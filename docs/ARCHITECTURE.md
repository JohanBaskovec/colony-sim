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
| `animal`     | Wild animals (`Animals`, a `Pool` and a fixed array): species definitions (speeds, needs, resting hours, flight distance, herd sizes, diet), needs, decisions on tile centers (drink, eat, sleep, back to the herd, stroll), grazing and eating what plants bear, fear of settlers (sight, hearing, alarm to the herd, flight), herds behind a leader, populating a generated world, hash |
| `colonist`   | Colonist data, needs, decision making, movement along paths, work, what its mood thinks of (states, sleep, meals) and its breaks, facial expression from the mood, the action its hands are doing |
| `appearance` | Looks of a colonist (skin, body shape, hair style and color, beard), rolled at creation |
| `designation`| Player work orders per tile (mine, which digs earth or rock, cut a ramp, dig down, take a structure apart), the knowledge, level and tool each needs, work progress |
| `dirt`       | How dirty each tile is (0-100 %): adding, spreading around a tile, cleaning; only on open ground |
| `reservation`| Who is busy with which tile (job, food): one claimant per tile and kind     |
| `item`       | Items on the ground: definitions, categories, quality (0 to 100) and hit points; every item a record of its own (a pool with generational slots, a list per tile, its spot and angle in the tile), a stack summary per tile for callers that count, dropping, transformations (drying) and rot item by item, change stamps per tile so that the known world copies only what changed, loads in hand that are the very items picked up, natural items (stones, branches and twigs laid by world generation and the trees: ordinary items) (docs/ITEMS.md) |
| `stockpile`  | Storage zones drawn by the player: tiles, priority (1-10), allowed item types, quality and hit point ranges, radius |
| `tool`       | Tool speed factors by kind of work (bare hands included) and quality, best carried tool, wear and breaking |
| `gather_zone`| Gathering zones: tiles, one order per resource (wood, each wild food, grass and fibres: gather N, until N in the colony or N days of food, forever; full harvest only) and per group of resources (wood, food, fibres: limits each of its resources), a zone priority, the jobs tiles offer (picking and digging, careful ones for skilled gatherers), what they give |
| `hauling`    | Queries: which stack needs hauling (any group of a tile), best storage tile for it (one group per storage tile) |
| `item_move`  | Item moves ordered by the player: the items of a tile, or one item, → chosen tile (`ItemMoves`, 64 orders), room at a destination, pruning |
| `recipe`     | Recipes: stations, inputs in preference order, tools, work, knowledge and minimum level; products, quality cap from materials, failures |
| `workstation`| Workstations (crafting spot, knapping spot, open hearth): definitions (materials, construction work), bills, the craft in progress, a hearth's fire state |
| `hearth`     | The fire of an open hearth: fuel values, fuel wanted, burning, lighting rules, light around burning hearths, the hours of fireside stories |
| `structure`  | Structure definitions (grass bed, ground bedding, grass and branch wall, branch screen): materials, work, knowledge, movement cost, rest factor, length in tiles and `Direction`, hit points, wear and refresh; hit points by quality, quality cap from the materials |
| `bed`        | Bed ownership: one bed per settler, one settler per bed, kept on the bed's head; beds gone from the map are forgotten |
| `construction`| Construction sites: blueprints of structures and of workstations, refresh sites on worn structures, materials delivered per slot, work, built quality; daily wear; built structures go to the map's structure layers, finished workstations to `Workstations` |
| `work`       | Work types (mining, fire, woodcutting, gathering, construction, crafting, hauling, cleaning), the knowledge shown for each, priority helpers |
| `knowledge`  | Knowledges (fire, stone knapping...), levels 0-100, mastery at 50, work speed, learning (rates, caps, fading), quality and condition of what a worker produces |
| `corpse`     | The record kept of each settler or wild animal that died (`Corpses`, a growing list: a copy of the `Colonist` or `Animal` as it was, and the tick), which the body's item stack points at; for the eye only, not hashed |
| `conversation`| Settlers nearby chatting now and then (`docs/SOCIAL.md` §6): knowledge talk and explanations, true stories and lies, compliments, insults, gossip and gossip passed on, arguments and brawls, apologies, small talk; how each chat went, the memories, company and journal entries it leaves; the phrases chats are told with |
| `social`     | What settlers think of each other (`Game.social`, by colonist slot): opinions from family ties, looks and memories that fade over hours to decades, families of the starting settlers, relationships with slack, grief, memories of violence; tales heard to pass on, wrongs and apologies, the company of friends and rivals; a random stream of its own |
| `experience` | What a settler lived through that makes a story (`Colonist.experiences`, a ring of 8): kills, attacks, deaths seen, losses, hardships; fresh ones told more |
| `notice`     | Messages the simulation raises for the player (a knowledge about to be lost, a settler breaking down...), the last 200 kept in a ring (the colony log) |
| `alert`      | What is wrong in the colony right now (critical needs, food for under a day, breakdowns, no bed), recomputed from the game state on each call; nothing is stored |
| `combat`     | Blows between beings, for settlers and animals alike: attacks (damage, injury kind, accuracy, interval, reach), hit chance against dodge, the limb an attack is made with and the attacks a species chooses among (`AttackChoice`, `combat_choose_attack()`), a blow through `health`, the hurt fraction and breaking off, the queue of strikes the game resolves, the mark each attack leaves on screen and the log of the last blows for the frontend (see `docs/COMBAT.md`) |
| `journal`    | The journal of a settler or an animal (a ring of the last `JOURNAL_MAX` entries, in `Colonist` and `Animal`): new thoughts, wounds (who struck with what, which part, what it caused), blows given and taken (missed, dodged, harmless, wounding, fatal; the same miss again soon counted in one entry), social life (chats and their topics, stories, lies, compliments, insults, gossip, relationships); facts only, the frontend writes the sentences; presentation only |
| `blood`      | Blood on the ground: drops on the steps of those who bleed, more and larger the worse, a pool under one who stays still, stains drying and fading (a ring of the newest; presentation only, see `docs/HEALTH.md` §9.1) |
| `strike_anim` | How a blow is played on screen, as functions of the ticks since it was resolved: the attacker drawing back and lunging, the one struck flinching and flashing, the mark drawn, held and fading (presentation only, see `docs/COMBAT.md` §5) |
| `health`     | Bodies for settlers and animals alike: body plans and parts, injuries that bleed and hurt, consciousness (the sum of what takes it off, the posture: up, collapsed, unconscious) and abilities (walking, the hands, sight, eating), clotting, natural healing, blood coming back, death; falls (see `docs/HEALTH.md`) |
| `attributes` | Settler attributes: positions in thousandths drawn on a bell curve, their effects (factors, thought weight, outlook points, temperature shifts), the word bands and translation keys |
| `mood`       | Moods: thoughts (states setting in and growing worse, memories; all fading over time), the mood as their sum, its effects on work speed, quality and learning, breaks (sulking, going off alone, binging) |
| `occupancy`  | Who stops on which tile, settlers and animals alike: one claim per tile (where a being stands, or where its path ends), yielding claims (idle, strolling), forced ones (knocked down), how many beings are on each tile now (crossing slows down), the pathfinder's stop filter |
| `pathfinding`| A* to a goal, Dijkstra to the nearest tile matching a predicate, on the start's level; for a kind of walker (`PathWalker`: settlers, or animals who open no door and climb neither ladders nor trees) |
| `region`     | Connectivity regions: which tiles can reach each other on foot, per level (a cache) |
| `tile_box`   | Box of tiles holding every tile of a sparse layer (designations, gathering zones, a region), so that scans skip the rest of the map |
| `tree`       | Trees in the volume: a trunk of solid wood and floors of foliage built into the map as the tree grows, finding the tree of a tile, where a felled tree falls (aimed away from structures) and what it leaves along its length |
| `roof`       | Roofs falling in and galleries caving in when their last support goes, checked around what the map logged as changed |
| `temperature`| Outdoor air through the seasons and the day, a temperature per tile: heat spreading, walls, roofs, caves, hearths, the microclimate under the sky (sun, shade, evaporation, heat stored in the ground) |
| `soil`       | The water in the ground of each tile (a bucket by terrain: rain and snowmelt, interception by crowns, drainage, evaporation and transpiration, banks near water), and what the microclimate needs from it (see `docs/CLIMATE.md`) |
| `vision`     | Who sees what: sight in 3D through the map's volume (rock, walls and floors block it, no range limit), fields of view per settler, how many see each tile (see *Vision*) |
| `weather`    | The sky every 6 hours (clear, cloudy, fog, rain, storm; snow when cold), warm and cold spells, snow cover, recent rain, rainfall in mm, the air's dew point and relative humidity |
| `pool`       | Fixed-capacity slot allocator with generational handles (`EntityId`)        |
| `game_clock` | Ticks, speed, pause, calendar (`GameDate`), daylight curve and the sun (day length by season)                |
| `sun`        | Where the sun stands: elevation, azimuth and direction (map axes, metres) from the day, the hour and the latitude; its declination gives the clock's day lengths at the default latitude; its rays (`sun_lit()`): how much direct sun reaches a point of the volume, through rock, walls, doors, floors and roofs (nothing) and foliage (its share by the length crossed) (see `docs/SHADOWS.md`) |
| `sky`        | The light of the sky: how much of the sky a surface sees (`sky_open()`, 16 rays over the dome through blocks, floors and foliage), and a field of it per column for the screen, worked out again where the map changed (see *Light and shade*) |
| `fire_light` | The light of fires on screen: from each point of the fine texture a ray to the flame (3D, stopped by walls, floors and rock), kept per fire until the fire or the map near it changes (see *Light and shade*) |
| `shadow`     | Shadows on screen: the sun's rays from the surfaces drawn, worked out ahead of time by pieces and half hours, each point keeping the moment an edge passes, so frames read them for the time now and the shadows glide; out of date by the map's light stamps; the light put together (sky, direct sun) (see *Shadows on screen*) |
| `map`        | The map as a volume of levels (`map_index()`, `map_ground_level()`), tile layers (terrain, rock, plants with a stock for each product, cobbles and flint in the ground, branches and twigs lying on tiles), static definitions (`TerrainDef`, `PlantDef` with the plants' seasons...) and a plant's life over the year (`plant_advance()`) |
| `worldgen`   | Procedural map generation from a seed: relief (hills, slopes, rock outcrops behind cliffs), terrain, trees, then the understory by the light under their crowns, stones |
| `noise`      | Deterministic value noise and fBm                                            |
| `rng`        | PCG32 random number generator                                                |
| `i18n`       | Translation catalogs, `tr()`, plurals, placeholders, number formatting       |
| `camera`     | World ↔ screen transforms, zoom, visible tile range, the level shown         |
| `cli`        | Command-line parsing (language, seed, size, colonists, test scenario, cheats) |
| `entry_strip` | Fits same-sized entries (the colonist bar) into a width: shrunk, then wrapped onto rows |
| `scenario`   | Ready-made test situations on a new game (`bench_simulation`, `bench_social`, `--scenario`): gathering, unreachable jobs, construction, feuds (two clans who loathe each other), cooking (two lit hearths, heaps of food fresh and half rotten, some of several kinds) |
| `test_scenario` | The F2 debug menu's scenarios: `test_scenario_load()` replaces the world of a running `Game` (shut down, then built again from the same seed, debug flags and clock speed kept) with a flat bare 96×96 world and places hungry wolves and a herd of roe deer, or two settlers (one with a thrusting spear), or eight settlers in two feuding clans, by hand |
| `settings`   | Player settings kept between sessions (interface size, palette, language) as a forgiving `key = value` text; the frontend loads and saves the file |
| `log`        | Logging with a pluggable sink                                                |
| `util`       | Small helpers (clamp, lerp, FNV hashes)                                      |

## Frontend modules (`src/game/`)

| Module         | Responsibility                                                          |
|----------------|-------------------------------------------------------------------------|
| `main`         | Parses the command line, runs the app                                   |
| `app`          | SDL init/shutdown, main loop, input → camera/clock, frame orchestration |
| `render`       | Draws the map (ground and wall textures, colored tiles for the other terrains), zones, items (each at its spot, turned its own way: every item type drawn once into a texture of icons, then textured quads in batches; the newest ten of a tile, one when zoomed far out), plants and colonists (sprites, depth sorted), blood on the ground, blows (lunges, flashes, marks), daylight overlay, highlights |
| `sprites`      | Loads `assets/sprites/*.png` into a texture atlas with halved copies, batches sprite quads (additive for a flash) |
| `strike_marks` | Draws the marks of blows as shapes (claw scratches, jaws, a slash, a burst); `tools/preview_attacks.py` draws the same into a GIF |
| `hud`          | Builds and draws the HUD panels and the windows in stacking order; all text goes through `tr()` |
| `theme`        | Color roles and every size of the interface, the UI scale applied (`--ui-scale`) |
| `layout`       | Zones of the screen (bottom bar, what lies above it, the free top corners), from the screen size and the theme |
| `ui`           | Immediate-mode widgets (panel, window, button, check box, slider, dropdown, tooltip with wrapped lines) and click capture |
| `ui_layout`    | Rows of a window laid out twice: measured, then drawn (window sized to its content) |
| `bottom_bar`   | Full-width bar along the bottom: Build, Work, Log, Menu                 |
| `alerts_list`  | The alerts column above the date: a line per alert, tooltip, click jumps to the first colonist |
| `log_window`   | The colony log: the notices kept, newest first with their dates, a docked main window |
| `build_menu`   | Build menu: category column, icon row, card of the hovered icon or the tool in use |
| `resources_list` | Top left: the stock by item category (`Game.stock`), groups folded with a click, tooltips with where the items lie and the days of food |
| `colonist_bar` | Top center: portrait, name and current action per colonist; click selects, double click centers the view |
| `view_controls` | Bottom right: temperature overlay, speeds, level slider (date and weather beside it) |
| `game_menu`    | Game menu (Menu button or Escape): resume, controls, settings, quit; pauses the game |
| `settings_window` | Settings (from the game menu): interface size, colors, language, applied at once and saved |
| `inspector`    | Bottom left: summary box of the selection, tabs opening a panel above it, action buttons beside it; dispatches to the panels below (colonists: `hud`) |
| `stockpile_panel` | Stockpile zone in the inspector: size and priority; Storage tab (radius slider, condition sliders, filters); build another, delete |
| `gather_zone_panel` | Gathering zone in the inspector: size and priority; Resources tab (foldable groups and their resources: tri-state check boxes, how many are available, the full harvest icon, a mode dropdown with target, unit and progress; warnings); build another, delete |
| `station_panel` | Workstation in the inspector: state, fire, work in progress; Bills (the active bills, and two windows docked to the right of the tab: the recipes to add, and the details of the one picked) and (hearths) Fire tabs; forbid, build another, delete |
| `animal_panel` | The selected wild animal in the inspector (`App.selected_animal`): what it is called as title (a hind, a stag), species, sex, activity, needs as bars with tooltips, herd, health |
| `journal_panel` | The Journal tab of a settler or an animal: five filter check boxes (`App.journal_hidden`; routine thoughts under Routine only), the entries newest first with their date, sentences built from the facts of `core/journal.h` (wording per language: verb by the strike mark, part and injury phrases, the owner's and the attacker's phrases by species; social sentences from topic, story and know-how phrases, with French elision before a vowel; a thought as a full sentence, its tooltip on hover) |
| `thought_tooltip` | The tooltip of a thought (what it is about, its first value, how it fades or grows, how it adds up), shared by the Mood tab and the journal |
| `health_panel` | The health of a settler (its Health tab) or an animal as table rows: the state, consciousness and what takes it off, blood, bleeding, pain, afflictions, abilities, injuries with tooltips |
| `tile_panel`   | The selected thing on a tile in the inspector (a structure, a roof, a site, the items, a plant: `App.selected_thing`): its name as title, what to know of it (for items, the list of every item lying there, sorted by newest, quality, condition or rot), and the action menu (forbid, fell, take apart, cancel a site, build another) |
| `work_panel`   | Work priorities grid (colonists × work types), a docked main window     |
| `debug_panel`  | Debug menu (F4): a check box per cheat (`GameDebug`), one-shot commands (knowledge, time, discord among the settlers, forgetting the debug opinions), the chats counted so far, the debug tool (move settlers) |
| `scenarios_panel` | Test scenarios menu (F2): a button per scenario; a click asks `App` to load it, which `app_run()` does at the start of the next frame (never while windows are being drawn), forgetting selection, tools and notices |
| `animals_panel` | Animals menu (F6): a Female and a Male button per species, which arm the debug tool to put one on every clicked tile |
| `items_panel`  | Items menu (F5): every item with a 1× and a Max button, which arm the debug tool to drop it on the next clicked tile |
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
stockpiles, zones, regions, temperature, pathfinder) sizes it for all
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

**Plants** (`core/worldgen.c`, docs/PLANTS.md §10): generation goes in
passes, each with its own random stream so that the others do not depend on
it. The first places trees and hazels (forest noise, glades, mostly grown
ones); then the slopes, the tree volumes (crowns) and the litter; only then
`place_understory()`, which asks `map_light_cover()` for every free tile of
ground and rolls its plants from the light, the ground, the moisture and a
noise per plant (patches); last, cattails and reeds on the shores. What lives
under a crown is therefore what the crown's shade allows.

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
- `plants[]`: at most one plant per tile (`Plant`: type, growth, and a stock
  per product, `PLANT_PRODUCTS_MAX` slots). Grass is a plant like the others:
  it takes the tile's one place, so no other plant grows on a tile with grass
  (a tree, a bramble or a patch of mushrooms is not planted over it, and a
  place left bare fills in again as plants spread, see below). A
  product (`PlantProduct`, `PlantDef.products[slot]`) is anything the plant
  gives without being felled that grows back: an item (blackberries, roots,
  an armful of fresh grass), a `capacity` at full growth, `regrow_hours` per
  item, the `days` of the year it is there, the `min_growth` the plant needs
  to bear any, and whether it is `dug` up or `confusable` with a poisonous
  plant. Stocks are counted in hours of regrowth, capped by the capacity
  scaled by growth, and grow back with the plant's hourly update, even on
  mature plants. A tree bears none (what it sheds lies under it, see below);
- `branches[]`, `twigs[]`: the dead branches and twigs lying on each tile (at
  most `MAP_BRANCHES_MAX` and `MAP_TWIGS_MAX`), on the ground under the
  trees (`map_litter()`, `map_add_litter()`, `map_take_litter()`).

**Plants live by the climate** (`docs/PLANTS.md`): each hour a plant grows at
its best rate (`PlantDef.growth_per_day`) times the response to the
temperature of its tile (`plant_temperature_response()`: a plateau between
`t_min` and `t_max`) and to its health, and the fertility. After a few days of
cold (`Plant.climate`, a running mean) it rests or dies back, and wakes with
the warmth; the cold hardens it (`Plant.hardening`); frost and heat beyond its
limits wear its health down (`Plant.damage`) and it is removed when that is
gone, a tree with a trunk excepted. The seasons come out of the temperatures
(`core/temperature.h`), not a calendar: a roofed room or a cave has a climate
of its own. Products ripen with the warmth the plant has gathered since it woke
(`Plant.warmth`, degree-days; `PlantProduct.warmth_from` and `warmth_until`,
and `lost_below` for the chill that spoils them: blackberries at the end of
summer, rhizomes and cut grass all year...). A `PlantDays` bit set (60 days fit
in 64 bits) only says when a tree lets its fruit fall (`PlantDef.fall_days`).
Out of season a stock is gone; a plant that died back (wild garlic in
the heat and the cold, nettles in winter) shows nothing above ground until the
warmth returns. `plant_advance()` does one such step (`update_plants()` calls
it once an hour per plant, a slice of the map per tick); `plant_settle()` puts a
new plant in the state of a day (world generation, planted food). The light a
plant meets is the sun (`game_sunlight()`: the day's length and strength follow the
season), the sky (`weather_light()`) and what covers its tile
(`map_light_cover()`: foliage, roof, rock); `game_plant_conditions()` gathers
all it meets. What
taking the products does to the plant is data (`PlantDef.harvest`,
`plant_harvested()`): `PLANT_HARVEST_KEEPS` leaves it as it is and the stock
grows back (fruit, leaves), `PLANT_HARVEST_RESETS` sends it back to growth 0,
growing again from what is left in the ground (a wild carrot dug up, grass
cut), `PLANT_HARVEST_REMOVES` takes the plant with its product (a mushroom
patch picked, which also goes by itself once its stock is gone). Grass is cut
as any plant is harvested: a stock of armfuls that follows its growth (one
armful grows back within the hour once the grass is tall enough), and it is
not food. What a settler or an animal eats from a plant is its one food
product (`plant_food_slot()`).
Mushrooms appear on their own in autumn on bare ground near mature trees: each tile gets a
daily roll from a hash of the seed, the tile and the day (`world_roll()` in
`game.c`), during the first in-game hour of the day, so that they draw
nothing from the game's random stream.
Plants spread the same way (`spread_day()`, docs/PLANTS.md §13): in that
first hour, each grown plant that is awake rolls its `PlantDef.spread_chance`,
picks a tile within its `spread_range` (`plant_spread_target()`), and a young
plant (`plant_offspring()`) takes root there when the tile suits it
(`plant_can_take_root()`, pure map rules: free, its ground, light in summer's
shade, water, room for a tree) and the colony uses nothing on it
(stockpiles, workstations, building sites, orders: checked in `game.c`; items
lying there do not stop it). Only bare tiles are taken: nothing pushes another
plant out yet.

Terrain and rock decide where one can walk. Once the map is in use they are
changed through `map_set_terrain()` / `map_set_rock()`, which bump
`Map.topology_revision` and log the change, so that caches derived from
passability (connectivity regions) know when they are out of date. Plants
never block movement.

**Trees in the volume** (`core/tree.h`, step 3d of the levels): a sapling
(the first two growth stages) is a plant like the others, walked through.
From `TREE_VOLUME_MIN_STAGE` on, a tree takes room like a building: its
trunk is solid wood (`ROCK_WOOD`, over open terrain above the foot) from the
foot up, and its foliage is a floor of branches around the trunk on its
upper levels, the top one covering the trunk too. The branches thin out
from the trunk in three densities, each a terrain: dense
(`TERRAIN_FOLIAGE`, walked on at ×1.5, nothing built on it), thin
(`TERRAIN_FOLIAGE_MEDIUM`) and sparse (`TERRAIN_FOLIAGE_SPARSE`), the last
two too weak to stand on (impassable, whatever falls on them goes through,
see `lets_fall()` in `tree.c`). `terrain_is_foliage()` is true for all three. The plant stays on the foot's tile (the
north-west one of a wider trunk) with the growth, stocks and trunk width
(`Plant.size`, 0 meaning 1); the shape follows from them (`tree_shape()`):

A tree of the default size grows continuously (`tree_shape()` takes the
plant's growth, 0..1; the first two growth stages are a sapling). From
`TREE_CROWN_FROM` (0.4) it takes room, and its trunk rises with its growth:

| Growth | Trunk (levels) | Crown |
|---|---|---|
| from 0.4 | 1 | the foliage over the trunk, sparse |
| from `TREE_SECOND_LEVEL_AT` (0.49) | 2 | tiles come in around the trunk on every level, nearest first |
| from `TREE_THIRD_LEVEL_AT` (0.55) | 3 | up to `TREE_CROWN_REACH` (2) tiles from the trunk, all in by 0.8 |

Old and ancient trees (only from world generation) have a fixed shape,
fully grown (`TREE_FULL_GROWTH`):

| Tree | Trunk | Levels of trunk | Foliage from level | Reach | Levels |
|---|---|---|---|---|---|
| Old | 2 × 2 | 5 | 3 | 2 | 6 |
| Ancient | 3 × 3 | 6 | 3 | 3 | 7 (`TREE_MAX_LEVELS`) |

The crown is rounded and ragged, and it grows tile by tile
(`tree_foliage_density()`, a function of the tile's offset from the foot
and `TreeShape.growth` only). The outline: a rounded disc, the further a
tile is from the trunk the likelier it never comes (`TREE_FOLIAGE_RAGGEDNESS`).
Each tile that does comes in at a growth that rises with its distance from the
trunk, blurred by noise (`TREE_CROWN_DISORDER`), within `TREE_CROWN_SPREAD`
of growth: nearest first, differently on each level. It comes in sparse and
thickens a notch every `TREE_DENSIFY_GROWTH` (about 3.3 days on plain soil)
up to the density of its ring around the trunk (1: dense, 2: thin; a tile of
an outer ring a notch thinner now and then, `TREE_FOLIAGE_PATCHINESS`). The
noise comes from the tile and `TreeShape.seed`, a hash of the foot's
position, nothing is stored: two trees differ, a growing tree only adds
foliage, and what it had never thins. A young tree has no walkable
foliage yet (only dense foliage is walked on) and cannot be climbed.
`grow_tree()` (game.c) rewrites the crown whenever the shape or growth
changed (every update hour of a growing tree), and `tree_build()` raises the
density of foliage already there (never lowers it: where two trees share a
tile the thicker wins). Thin foliage is seen through: `map_view_level()` (what the tile selection,
zones and the drawing call "what is seen") goes through it like through open
space, and the renderer draws its tiles over the levels below with textures
of leaves and bare branches on a transparent background
(`ground_foliage_medium` and `_sparse`); things fall through it
(`map_sees_through_at()`). `map_set_terrain()` tells the connectivity regions
(`core/region.h`) only of a floor appearing (simple to add) or going: thin
foliage coming and going leaves them alone. Old and ancient
trees only come from world generation (`OLD_TREE_CHANCE` 4 %,
`OLD_TREE_CHANCE` 4 %,
`ANCIENT_TREE_CHANCE` 1 % of the grown trees where the wider trunk fits);
their stocks scale with the trunk's area, as does the wood they give.
`tree_build()` writes a tree into the map through `map_set_rock()` /
`map_set_terrain()`: foliage only fills open space, not over a slope or a
ladder (whose way up it would close). When a growth stage changes the
shape, the game checks that the trunk has room (`tree_room_to_grow()`: no
rock, structure nor settler where it would go) and builds it, or waits for
the next growth step; what lies where the trunk rises is pushed onto the
first open tile beside it (`push_items_off_trunk()`, as a wall built there
does), and stockpile cells under the new trunk go.
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
a random stream of its own). What a tree sheds falls through its crown, of
any density, and lands on the ground under it (`litter_landing_level()`): it
never stays in the foliage, so nobody climbs for it. What lay on foliage that
goes with a felled tree (items) falls to the ground below (`tree_clear()`; what does not fit lies as items). No plant
bears branches or twigs of its own any more.

**Hazels** are trees too (`plant_is_tree()`: `PLANT_TREE` and `PLANT_HAZEL`;
a seedling below growth 0.4 is a shrub sprite, as for the other trees). A
hazel has no wide trunk, at most two levels of trunk
(`TREE_HAZEL_SECOND_LEVEL_AT`), a crown that grows like the others', less
wood (`PlantDef.cut_yield`) and lets its nuts fall in autumn
(`PlantDef.fall_item`, `fall_days`, `fall_yield`): once an hour per tree
(`tree_fall_chance()`: the season's yield spread over its hours, scaled by
growth) a nut is shed by `tree_shed()` like a twig, onto the ground under the
crown. Nuts lie in a map layer of their own,
`Map.nuts` (`MAP_NUTS_MAX` a tile, `map_litter()` and friends take
`ITEM_HAZELNUTS`), have no state of their own and **rot on average**
(`rot_nuts()` in game.c, with the items' sweep): each hour a tile with nuts
loses as many as `item_rot_rate()` of its conditions says over
`ItemDef.rot_days` (90 days at 15 °C), the fraction rolled from the seed, the
tile and the hour. Gathering zones pick them up on `GATHER_JOB_NUTS` (all the
nuts of the tile at once), and a hungry settler eats the nuts lying where it
stands.
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
(`map_hash()`). Upcoming layers: floors, buildings/walls, zones,
temperature, fog of war.

Player orders are not part of the map (which only describes the physical
world): `Designations` (`core/designation.h`, owned by `Game`) has its own
dense layers of the same size: order type and work progress per tile.

**Digging** (step 3 of the levels, first part): earth (`ROCK_EARTH`) is dug
with the shelter builders' knowledge (`DESIGNATION_DIG`, a pit dwelling or
a gallery into a hill; its ground stays overhead as a natural roof), rock is
mined (`DESIGNATION_MINE`, nobody can yet). The player has one order, "Mine":
`designation_set()` stores `DESIGNATION_DIG` on an earth tile and
`DESIGNATION_MINE` on rock, as they need different knowledge and tools (same
name, no menu entry of its own). Both are "Digging" work (`WORK_MINING`). `DESIGNATION_DIG_RAMP` cuts a ramp into a bank of earth
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

**Harvest and Uproot** (`DESIGNATION_HARVEST`, `DESIGNATION_UPROOT`,
"Gathering" work): orders on a single plant, outside any zone. Harvest takes
what the plant bears without felling it, with the zones' job for it
(`gather_plant_harvest_job()`: picking its food, digging roots up, cutting
grass: what happens to the plant is `PlantDef.harvest`, as in a zone); it is refused while the plant
has nothing to take (`gather_plant_harvestable()`), and dropped when what it
bore is gone before the work is done. Uproot pulls any plant but a tree up:
what it bears is harvested first, then the plant goes (`gather_clear_plant()`,
as when a building site is cleared); its work is the harvest's plus the
clearing's (`gather_uproot_work()`). The plant decides the level and the tool
(`designation_min_level_at()`, `designation_tool_use_at()`): a plant easily
mistaken for a poisonous one needs `GATHER_CAREFUL_MIN_LEVEL`, roots the
digging stick; work searches and the action menu check it on the tile
(`colonist_can_do_at()`, `game_designation_available_at()`).

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
`Game`): every item a record of its own in a list per tile, items of any
types side by side as long as there is room (see below). The items of a type
(and, for bodies, of the same dead one) form a group seen as a stack; the
tile's summary (`Items.stacks`) is the group of its newest item and
`Items.mixed` marks the tiles holding other groups too, so that most lookups
stay one read: the `*_of()` functions (`items_get_of()`,
`items_pick_up_of()`, `items_take_of()`) find the newest group among some
types, `items_groups()` lists them all, `items_lift_all()` and
`items_move_all()` take every group off a tile (a floor dug away, a wall
built), each item as it was. Searches ask for the types they want, pickups
take those types; a hauler remembers the type it collects
(`Colonist.haul_type`). Storage keeps one group per tile
(`haul_storage_room()`): haulers bring items only to an empty tile or one
holding the same group alone, and items dropped keep off the storage tiles
they would mix (`items_set_drop_filter()`, set by the game to
`haul_drop_filter()`: a load put down, berries picked from a bramble on a
stored heap, a craft's products). The holders of items keep the very items
too: a settler's load, the input of a craft in progress
(`Workstation.work_input`) and the materials delivered to a site
(`ConstructionSite.delivered`, `construction_deliver_load()`), given back as
they were when the craft or the site is called off and used up when it is
done (`items_load_move()`); a hearth's fuel is kilograms, burnt as it goes.
Settlers eat the items of a heap that rot away first
(`items_soonest_rotting()`), and look for food that is about to rot by its
worst item, not the heap's average. `items_check()` and `game_check_items()` check
that all this stays consistent (lists, summaries, mixed marks, counts per
level and type, forbidden tiles, every slot of the pool lying, held or free
exactly once, loads, craft inputs and site materials matching their items): the long runs of
`test_item_simulation` call them every in-game hour, `bench_simulation` at
the end (every hour with `BENCH_CHECK_ITEMS`). `Items.rotted` counts what
rotted away, by type. There are also
map-wide totals per type and a count of tiles with items per level
(`level_stacks`: haulers' searches skip the empty levels, underground ones
first). Each `ItemDef` has a realistic `weight` (kg per
unit: a 7 kg log, a 20 kg stone block, a 1 kg cobble, a 300 g handful of blackberries) and a `bulk`
(room taken, about half a liter per unit, gaps included). A tile holds
`ITEM_TILE_BULK` (1000) of bulk, so the stack limit is derived:
`item_stack_limit() = ITEM_TILE_BULK / bulk` (1,000 handfuls of blackberries, 50 logs, 50
stone blocks, 666 cobbles); items of several types share it by bulk
(`items_room()`). `items_drop()` fills the target tile, then
spreads the rest breadth-first over nearby passable tiles (never behind a
wall), on the tiles the drop filter accepts first. Products are data: `PlantDef.cut_item/cut_yield` (scaled by growth) and
`RockDef.mined_item/mined_yield`, `PlantProduct.item` (the plant's stocks);
gathering takes items from the plant's stocks (`plant_take_stock()`).

Some items change while they lie on the ground: `ItemDef.transforms_into`
after `transform_hours` (fresh grass dries into dry grass in 72 hours).
`ItemStack.progress` counts the hours spent so far; merging stacks averages
it by count, like hit points. A stack that transforms keeps its hit points
percentage, and its quality when the new type has one.

`ItemStack.eaten` (bodies only, 0 when whole; averaged on merges, hashed only
when not 0) is the share of a body's weight eaten away: `item_stack_mass()`
is what the stack weighs now, and what hauling and the panels use.

`ItemStack.corpse` (bodies only, 0 otherwise; hashed only when not 0) is the
id of the record of the settler or animal whose body it is (`core/corpse.h`,
`Game.corpses`). It is part of what makes stacks match
(`item_stacks_match()`): the bodies of two beings never share a tile, and the
id travels with the stack when it is carried, put down or bitten into.

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

Every item has a **quality**, a value from 0 to 100 (`ItemQuality` is an
`int`: `ITEM_QUALITY_WORST` 0, `ITEM_QUALITY_STANDARD` 50, `ITEM_QUALITY_BEST`
100), and **hit points** (`hit_points`, up to `ItemDef.max_hit_points`). Some
types are always perfect (`has_quality` / `damageable` false: stone blocks are
always of quality 100 and at full hit points); `item_stack_make()` enforces
it, so no code path can create a damaged stone block. Items of the same type
share a tile whatever their quality (`item_stacks_match()` compares the type
only); a stack keeps the count-weighted average quality and hit points of its
items. A storage filter judges a stack by that average, so a stockpile range
of qualities is a rule on the average of what lies there. Quality carries over
to what is made from the items (M3): its effects are continuous (tool speed,
a structure's hit points, §Tools and §Structures).

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
level sets a quality (25 + 1.4 × (level − 50), the score is the quality) and a
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
  is free to chat (`colonist_can_chat()`) a chance of two chats per hour
  together (`conversation_chance()`: sociability, confidence, opinions,
  loneliness), drawn from the social stream (`Social.rng`) in slot order. A
  chat lasts 6 minutes (`chat_ticks`, a bubble over their heads); 20% are
  about a knowledge, each speaker picking one weighted by level (×2 if
  practised in the last day), and the listener gets the whole chat's gain at
  once (cap 30 and the speaker's level). The others are stories, lies,
  compliments, insults, gossip or small talk (`docs/SOCIAL.md` §6).
- *Social life* (`core/social.h`, `docs/SOCIAL.md`): `Game.social` holds a
  `SocialBond` per ordered pair of colonist slots (what `who` thinks of
  `whom`): the family tie and up to 8 memories, each a value and the game hour
  it was last renewed, so that its fading (plateau, then a half-life) is
  computed when the opinion is asked for, never stepped; nothing ticks per
  pair. The opinion adds the tie, the other's beauty and confidence and the
  memories, clamped to ±100. `social_update()` runs once an hour
  (`GAME_SOCIAL_UPDATE_PHASE`): it drops forgotten memories and moves
  relationships with slack, noting changes in the journal. A slot is cleared
  when a settler is removed or spawned in it. Families and the journey are
  drawn by `social_start_together()` after the starting settlers are placed.
  Hooks in `game.c`: `settler_died()` (grief by family or friendship,
  experiences of the mourners and witnesses), `note_fight_lived()` after a
  wounding blow (experiences, memories of violence between settlers), the
  hardships noted from the mood's news before the journal takes it. Each
  listener keeps a few `SocialTale`s (gossip heard, to pass on). After the
  chats, every `CONVERSATION_CHECK_TICKS`, `social_company_update()` sets
  `Colonist.social_states` (a friend or a rival close by) from the
  relationships last noted, and `Colonist.friend_slots`, `close_friend_slots`
  and `rival_slots` (bit masks by slot), which `seek_company()` reads to pick
  whom an idle settler walks over to; the colonist ORs the states into its
  mood states, so that neither the mood nor the colonist module knows
  anything of opinions. Brawls hurt through
  `health_hurt()` with `HURT_BRAWL`, like falls, and are noted by
  `note_pending_hurts()`. Part of `game_hash()` (bonds, tales, the social
  stream, company, social states and experiences); `Game.chat_counts`
  (`ConversationCounts`, what the chats turned into) is not. The debug tools
  (`game_debug_chat()`, `game_debug_relation()`...) change opinions through
  a memory of their own (`SOCIAL_MEMORY_DEBUG`, set rather than stacked,
  fading over decades), so that the tooltip shows what they did and
  `game_debug_forget_social()` undoes it; `bench/bench_social` prints a
  colony's social life day by day.
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
  the frontend shows the new ones below the colonist bar, centered, each in
  a box of its own, for 15 seconds (fading out over the last 2). The colony log
  window lists all those the ring still holds. *Alerts* (`core/alert.h`) are the
  other way to tell the player: not events but states (a settler about to
  starve), found again from the game each time, so they need no storage and
  vanish with their cause.

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
ground, too cold, hot or wet (soaked) for a quarter of it, or well),
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

**Attributes** (`core/attributes.h`, `Colonist.attributes`, part of
`game_hash()`; design in `docs/ATTRIBUTES.md`): each settler has nine
positions from −1 to +1, kept as integers in thousandths and drawn by
`game_spawn_colonist()` on a bell curve (twelve uniform draws, standard
deviation 0.3) from a stream of their own (`GAME_ATTRIBUTES_SEED`, settler
slot and generation), so that neither the game's random stream nor the other
settlers depend on them. Effects are continuous: a factor 2^p on walking
(`walk()`), work pace (`colonist_work_speed()`), learning
(`colonist_learn()`), sleep (the rest gained) and appetite (the food need
falling); cold and heat tolerance shift by up to 5 °C the comfortable range
(`colonist_cold_limit()`, `colonist_heat_limit()`: sleeping well, the cold
and hot states, the nights remembered, the warm spot beside a fire); outlook
adds up to 15 points to the mood and temperament scales every thought's
weight from ×0.6 to ×1.5 (`Mood.outlook`, `Mood.sensitivity`, set by
`colonist_set_attributes()`; `mood_thought_value()` shows what a thought
weighs with it counted). `attribute_word_key()` gives the word of a position
from five bands (ordinary shows nothing). Tests make settlers ordinary with
`make_ordinary()` (`tests/test_settlers.h`) when they count on timings.

### Stockpiles and hauling

`Stockpiles` (`core/stockpile.h`, owned by `Game`) is another dense layer: the
zone index of each tile (0 = none) plus a fixed table of zones (number shown to
the player, priority from 1 = highest to 10 = lowest, default 5, and a bit mask
of allowed item types, a range of accepted qualities and a range of accepted hit
points in percent of the maximum). `stockpile_accepts()` checks all three;
items that are always perfect count as superb and 100 %. A zone also has a
radius (32 tiles by default, `STOCKPILE_RADIUS_UNLIMITED` for none): it only
takes items within it, measured in a straight line from the item to the zone's
bounding box, a level counting as a tile (`stockpile_in_radius()`: a few
integer operations, cheap enough for every tile a hauling search looks at; a
path cost from each zone would not be, and an L-shaped zone reaches a little
further than its tiles). The slider of the inspector offers the radii of
`stockpile_radius_of_step()`, every tile near the zone, coarser further away,
unlimited last. Zones only cover passable, buildable tiles. Every change
bumps a `revision` counter.

Drag tools (zones, orders, structures, the selection box) keep the corner
where the button was pressed as a point of the world (`App.drag_world_x/y`), not
of the screen: when the camera moves under a held button (pan, zoom, level), the
rectangle stretches towards the cursor instead of sliding with the view. The
anchor has no level: changing level under a held button moves the whole
rectangle (or selection box) to the level shown, which is where it is applied.

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
The tools work on what is seen: they call the `*_add_view_rect()` /
`*_remove_view_rect()` variants, whose `z` is the level shown, and each tile of
the rectangle stands for `map_view_level()` of it, the level itself or, where
it is open space, the first level below that is not, however deep (what the
renderer shows there). A zone drawn
from a high level thus covers the ground it sees below too, and is drawn in
the blurred view of the levels below.
Picking works on what is seen too: `game_colonist_at()` and
`game_colonists_in_rect()` take the level shown and also return the colonists
on a lower level that `map_view_level()` shows over their tile (a click and the
selection box alike).
Building does too: `game_build_level()` turns the level shown and a tile into the
level to build on (what is seen there; a roof goes on the open level over it),
`game_plan_structure_rect()` / `game_place_structure_rect()` take the level
shown and resolve each structure's tile, and the frontend resolves a
workstation's tile the same way (`game_place_workstation()` itself takes the
real level).
The other way round, `game_place_workstation()` takes the station's tile out
of the zone it was in (blueprints included). Tiles
are taken out of zones with Shift+drag or the removal tools of the build menu;
a zone that keeps some tiles stays, even in several pieces.

A stack **needs hauling** when no zone on its tile accepts it (narrowing a
filter makes stored items move out), or when a zone of strictly better
priority accepts it, reaches it (its radius) and has a free tile with room. A
storage tile has room for
a stack when it is empty or holds a matching stack that the zone still
accepts (`haul_destination_room()`). Hauling
is the `WORK_HAULING` work type: at each priority level, designations come
first, then hauling. `haul_find_job()` runs a Dijkstra search for the nearest
such stack, then one search per priority level (best first) for the nearest
storage tile from the stack. Once per search, the tiles of the zones are
visited (each zone keeps its bounding box: the map is never scanned, and a
zone with an empty tile is not scanned further) to record which zones have
room for which type, plus a per-type bit set of "priority levels
with room". The latter is a quick upper bound that lets idle colonists skip
the search entirely when nothing can be stored; the per-zone data, checked
against the zone filters, decides for a given stack.

The hauler reserves the source stack (`RESERVATION_HAUL_SOURCE`) and the
destination tile (`RESERVATION_STORAGE`), walks to the stack and picks up what
the destination can take and it can carry. With room left in hand, it then
collects more on the way (`haul_find_extra_source()`): the nearest stack
within `COLONIST_HAUL_COLLECT_MAX_COST` that matches the load (same type and
quality), that the destination's zone accepts and that no better zone with room
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
another storage tile, else drops its load.

**Item moves** (`core/item_move.h`): the player can order the items of a
tile carried to any tile a settler can stand on (`game_order_item_move()`,
from the *Carry elsewhere* action of the tile's items window), or a single
item (`game_order_single_item_move()`, from the window of that item:
`ItemMove.item` names it, an `ItemId`, which an item keeps while carried).
The order forbids what it is about, so nobody else takes it; an order on a
tile replaces the orders on single items lying there. Haulers try these
orders before the ordinary hauls (`try_forced_haul_job()`, which needs no
stockpile; `Colonist.haul_forced` marks the job, `Colonist.haul_item` the
item): the nearest unreserved source they can walk to, picked up whatever its
forbidden flag (`items_pick_up_group()`, `items_pick_up_item()` for one
item), carried as much as the settler can at a time (several trips for a big
stack, no gathering on the way), put down on the destination if it has room
(`items_put()`), forbidden (`items_load_set_forbidden()`) so that no hauler
takes it to storage; the player allows it when they want. An order that is
taken back drops what the settler holds; one whose source is empty, or whose
item left its tile, goes (`item_moves_prune()`, not while a settler is still
delivering the last load). `ItemMoves.revision` is part of the haul search stamp. The
orders are in `game_hash()` only when there are some. Any interruption (eating,
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

Colonists were the first entity type; wild animals follow the same pattern
(`Animals` in `core/animal.h`, a pool of `ANIMAL_MAX`); items and buildings
with state will too.

### Colonist behavior

Each tick, `colonist_tick()`:

1. updates needs (food and water always decrease; rest decreases, or
   increases while sleeping);
2. when standing on a tile center, decides: hungry or thirsty → walk to the
   nearest edible plant or drinking spot (Dijkstra search), most urgent need
   first; tired, or night and not fully rested → eat/drink first if below
   50 %, then sleep in place; otherwise wander nearby after a random pause.
   Each settler's night (`colonist_is_night_for()`) comes up to
   `COLONIST_BEDTIME_SPREAD_TICKS` (a quarter of an hour) later than 22:00
   and 06:00, after its slot, so that they do not all look for food, a bed or
   work on the same tick;
   Severe hunger or thirst wakes a sleeping colonist up;
3. acts: walks one tick along its path (speed / tile cost), eats, drinks, or sleeps.

A drinking spot is a passable tile that is shallow water or touches water
(`map_is_drinking_spot()`), so colonists stop on the shore rather than wade
in. A failed food or water search floods everything within its cost limit, so
retries back off exponentially (½ h, 1 h, 2 h, then every 4 in-game hours)
and reset on success. The starting position prefers fertile land with water
within reach. The first settlers bring nothing, so world generation then
plants grown brambles (bearing fruit at the start) and hazels (small trees,
whose nuts fall in autumn) nearby until there are `GAME_SPAWN_BRAMBLES` and `GAME_SPAWN_HAZELS`
within `GAME_SPAWN_FOOD_MAX_COST` of travel.

Decisions only happen on tile centers, so a colonist never jumps back when it
changes its mind.

### Wild animals

`core/animal.h`: plant eaters and carnivores that live their own life
(wildlife steps 1 and 2; births come later). `Game.animals` is ticked after the
settlers (`animals_tick()`, slot order) with its own `Rng`
(`Game.wildlife_rng`): what animals do never shifts the settlers' draws. A
game has animals only when asked (`NewGameParams.wildlife`, a multiple of
each species' density; the game as played asks for `GAME_WILDLIFE_DENSITY`,
`--animals <percent>` changes it): tests and benchmarks that are not about
them have none, and `game_hash()` only includes animals once one exists, so
their hashes are what they were.

- **Species** (`AnimalDef`): roe deer, red deer, wild boar, hare; the wolf and
  the fox (carnivores, below); walking and
  fleeing speeds, hours for each need to run out, the hours of the day the
  species rests in (deer at night, boars and hares by day), flight distance,
  herd sizes by sex, density, and diet: how it grazes (growth needed, growth
  a bite takes, what a tile of grass is worth), the plants whose food it
  eats, whether it digs roots up and eats fallen nuts, and what a unit of
  nutrition is worth to it. What it grazes is data too (`PlantDef.grazed`:
  grass).
- **Needs**: food, water, rest, from 1 down to 0 like the settlers'. Nothing
  happens at 0 yet (no health).
- **Decisions** on tile centers with nothing under way (`decide()`): hunger or
  thirst below `ANIMAL_URGENT_NEED` first; a follower far from its leader
  walks back; sleep when worn out, or a little tired in its resting hours;
  then water below `ANIMAL_THIRSTY` and food below `ANIMAL_HUNGRY`; else a
  stroll (around its leader for a follower) or standing there. Failed food
  and water searches wait longer each time, like the settlers'.
- **Eating** (`animal_food_at()`, `bite()`): a treat within
  `ANIMAL_TREAT_SEARCH_MAX_COST` first (the food a plant of its diet bears,
  nuts on the ground), else the nearest food, grazing included. A bite takes
  `ANIMAL_BITE_TICKS`: items off the plant's food stock (the very stock
  gatherers pick: animals compete with them), nuts off the tile, or some of the
  growth of a plant it grazes (grass: cutting it needs more growth than
  grazing leaves, and its stock of armfuls follows the growth). What a
  harvest does to a plant (`plant_harvested()`) applies to a bite too: a root
  dug up whole goes back to growth 0, an eaten mushroom patch is gone. It eats
  until `ANIMAL_SATED`.
- **Fear**: every `ANIMAL_ALERT_TICKS` an animal on a tile center looks out
  (`animal_notices()`): a settler seen within its flight distance (a straight
  line over the tiles between them; rock, walls and doors stop it), or
  within a third of it seen or not (heard), half as far when it sleeps. It
  then runs (`flee()`: the nearest tile `ANIMAL_FLEE_MARGIN` beyond its
  flight distance from every settler, at its fleeing speed, faster than any
  settler walks) and its herd mates within reach are alarmed and run too.
  Cornered, it stays and waits `ANIMAL_CORNERED_TICKS` before it looks for a
  way out again. Searches for food, water and strolls leave out tiles within
  the flight distance of a settler. Settlers ignore animals.
- **Carnivores** (`AnimalDef.carnivore`; design in `docs/COMBAT.md` §6): the
  wolf (packs of three to five, hunts deer, roe deer and hares, settlers
  when desperate) and the fox (alone, hunts hares). They eat carcasses only.
  Hungry (below `ANIMAL_HUNGRY`), a carnivore first looks for a carcass
  (`is_carcass()`: an animal's, never a settler's body), then a prey in sight
  within its `hunt_range` (`pick_prey()`; a settler counts twice as far and
  only below `ANIMAL_DESPERATE`) and attacks it, its herd mates with it
  (`start_hunt()`). A carnivore eats a carcass a bite at a time
  (`eat_carcass()`, `ANIMAL_CARCASS_BITE_TICKS`): a bite takes `bite_kg` of the
  carcass's weight (`item_stack_eat()`: `ItemStack.eaten`, a share in
  1/`ITEM_EATEN_FULL`, never its hit points, which only rot wears down; the
  carcass is gone with the last bite) and fills that over `stomach_kg` of its
  hunger. Prey see them (`Animals.predator_slots`,
  recomputed every tick) and run like from a settler, the herd with them; a
  carnivore fears settlers unless desperate, never other carnivores.
  Running tires (`Animal.fatigue`, `AnimalDef.sprint_seconds`): a spent animal
  runs at a third of its speed, so a hare outruns a fox only for a while.
- **Herds**: each animal has a `leader` (itself for a leader and a loner);
  followers stay within `ANIMAL_HERD_RADIUS` of it, so the herd roams as its
  leader does. `animals_set_leader()` keeps herds one deep (the leader
  followed is always the head); a leader removed is replaced by its first
  follower.
- **Moving**: paths from the game's `Pathfinder` as `PATH_WALKER_ANIMAL`
  (slopes, but no door unless kept open, ladder nor tree), on the real map (animals know
  their world; the fog is the colony's). Each step is checked as it is taken
  (`path_walker_can_step()`).
- **Populating** (`animals_populate()`, from `game_init()`): per species,
  density × land tiles / 10,000 animals, in herds of the species' sizes,
  on land that leads to water when there is some, `ANIMAL_SPAWN_CLEARANCE`
  tiles at least from where the settlers arrive.
- **Fights**: see *Combat* below: an animal struck (or sent by the debug tool)
  fights back or runs; hunting is how carnivores come to attack.
- **Seen or not**: `game_animal_seen()` is true where a settler sees the
  animal's tile now (always without the fog); the frontend draws and picks
  only those (`game_animal_at()`), and drops a selected animal that goes out
  of sight. Under the fog, an animal standing on a tile when the tile goes out
  of view stays there as a ghost (`KnownAnimal` in `KnownWorld.animals`, by
  slot: `known_update_animals()` after `sync_known()` each tick), drawn still,
  under the veil, with "seen here ... ago" on the tile; it goes when the tile
  is seen again or the animal is seen elsewhere. One seen walking out of view
  leaves none (its last spot is still in view). Ghosts are never picked.

Cost: about 3 µs a tick for 70 animals (200 × 200 map, Release);
`bench_simulation ... wildlife` prints how they fare (what they do, their
needs on average and at worst).

### Health

`core/health.h` (design in `docs/HEALTH.md`): a `Health` lives in each
`Colonist` and each `Animal`; only the body plan and the size differ. Blows
land on a part (`health_hurt()`, `health_blow()`, `health_fall()`); each tick
of a hurt being `health_update()` runs bleeding, clotting, healing and blood
coming back, and says when it died. Abilities are read from the state when
needed (`health_ability()`): nothing derived is stored but the posture
(consciousness, below), and a healthy body
returns exactly 1, so that the simulation and `game_hash()` stay as they were
while nobody is hurt (`health_hash()` adds nothing then).

- Settlers: the Toughness and Pain tolerance attributes set the damage and
  pain factors (`colonist_set_attributes()`); walking scales the walk
  (`colonist_walk_factor()`), the hands the work speed and the weight carried
  (`colonist_max_carry_weight()`). Collapsed or unable to walk, the colonist
  is `TASK_DOWN`: `colonist_tick()` only runs its needs; it gets up by itself.
  An unconscious settler is no viewer (`update_vision()`); a collapsed one
  frightens no animal and cannot strike.
- Animals: `AnimalDef.body`, `.size` and `.carcass`; `ANIMAL_TASK_DOWN`; a
  character of their own (`Animal.attributes`, `attributes_roll_animal()`, the
  effective values in `animal_walk_speed()`, `animal_run_speed()`...).
- Animals and the weather: `AnimalDef.cold_below` / `heat_above`;
  `animal_cold_pressure()` / `animal_heat_pressure()` from the tile's
  temperature (`AnimalContext.temperatures`); `try_shelter()` in `decide()`
  looks for a tile warmer or cooler by `ANIMAL_SHELTER_GAIN`
  (`ANIMAL_SEARCH_SHELTER` delays failures), and `try_wander()` keeps a
  comfortable animal from strolling out of its spot.
- Deprivation (`health_strain()`): every `COLONIST_STRAIN_TICKS` (settlers)
  or `ANIMAL_STRAIN_TICKS` (animals), spread over the beings, food or water at
  0 and the felt temperature (`colonist_cold_pressure()`,
  `colonist_heat_pressure()`) press on the severities in `Health.deprivation`.
  `health_is_hurt()` is true while any is above 0, so hashes of games where
  nobody suffers are unchanged.
- Temperature ranges (`TemperatureRanges`, `core/temperature.h`): what a
  being finds pleasant (no mood penalty for a settler, no urge to shelter for
  an animal) and what it bears without suffering;
  `colonist_temperature_ranges()` (mood thresholds and hypothermia /
  heatstroke, shifted by the settler's tolerances, the same ranges as the
  pressures) and `animal_temperature_ranges()` (its cold and heat limits,
  narrowed by the shelter margins). `temperature_feel()` names a felt
  temperature (freezing, cold, pleasant, hot, scorching); the Health tab
  shows it with `colonist_felt_temperature()` (hearths, wetness).
- Consciousness (`docs/HEALTH.md` §11): `health_consciousness_loss()` per
  `ConsciousnessSource` (the deprivations, blood, the head, pain), summed;
  the only stored part is `Health.posture` (up, collapsed, unconscious),
  which `settle()` updates after every change (`health_hurt()`,
  `health_update()`, `health_strain()`, `health_set_factors()`) with margins
  between falling and getting up, and where the deadly sources taking
  everything kill. `health_vigor()` is the one factor consciousness puts on
  the body: walking, the hands and the limbs, `combat_aim()`, an animal's
  sprint, recovery and alertness, a settler's learning and work level. The
  game tells the player when a settler collapses (`Colonist.collapse_noticed`,
  `NOTICE_SETTLER_COLLAPSED`); `ALERT_IN_DANGER` counts those under 70 %.
- Death: the game deals with it after each turn (`settler_died()` in
  `update_colonists()`, the carcasses in `update_animals()`): the body is an
  item (`ITEM_CATEGORY_BODIES`, rotting like food, left out of new
  stockpiles) that points at the settler's record (`corpses_add_settler()` in
  `settler_died()`: name, age, character, knowledge, health and journal, kept
  after the colonist's slot is freed; an animal's carcass likewise,
  `corpses_add_animal()` in `update_animals()`: species, sex, character,
  health and journal), the colonist goes through
  `game_remove_colonist()` (bed,
  reservations, tools), a notice, memories (`THOUGHT_DEATH`,
  `THOUGHT_SAW_DEATH`); the last one dead sets `Game.colony_lost` and pauses
  the clock (`game_update()` stops there). A dead animal's herd follows
  another leader (`animals_remove()`).
- Injuries from the world: falls in `step_out_of_blocked_tile()`; galleries
  caving in (`roofs_collapse()`'s tiles) and felled trees, which the map logs
  (`Map.tree_fall_log`, read by `hurt_under_felled_trees()`), hurt whoever
  stands there; `game_debug_hurt()` for the debug tool.

### Combat

`core/combat.h` (design in `docs/COMBAT.md`) only resolves blows. Who fights
whom is decided by the behaviors: a struck or ordered animal gets an
`Animal.enemy` and the task `ANIMAL_TASK_ATTACK` (`fight()` chases it, plans
the path again at every tile center and queues a strike when adjacent and
ready); a struck settler gets `Colonist.enemy`, and `engage()` in
`colonist_tick()` makes it `TASK_FIGHT` or, hurt past its nerve (the Courage
attribute), `TASK_FLEE`. Both only push `CombatStrike`s into
`Game.combat`; `resolve_strikes()` runs them at the end of the tick, in the
order they were asked, with the game's own `combat_rng`, then tells the target
(`provoke()`): an animal fights back or runs (`animals_provoke()`), a
settler fights or flees at its next turn (`colonist_provoke()`) and the
player is told. Combat is invisible to the hash until a blow is struck.
An attack is made with a limb (`BodyLimb`, carried by the parts of a body
plan: `BodyPartDef.limb`, `health_limb_ability()`); an `AnimalDef` lists up to
`COMBAT_ATTACKS_MAX` attacks with weights, and `fight()` draws one per blow
among those whose limb is whole enough (`combat_choose_attack()`, with the
animals' random stream, none drawn when only one is possible); a settler's
single attack (`colonist_attack()`) is `ATTACK_NONE` without the use of its
arms. The debug tool (F4) names an attacker and a target (`game_debug_attack()`).
Each resolved blow is also pushed into `Game.combat_log` (a ring of
`CombatEvent`s: tick, attacker, target and where it stood, attack, hit), which
no simulation code reads and `game_hash()` leaves out. The frontend plays it
afterwards (`core/strike_anim.h`): a blow is resolved at once, the screen
shows the attacker drawing back and lunging, and the mark (`AttackDef.mark`)
when the lunge lands, `STRIKE_ANIM_HIT_TICKS` later; in ticks, so it follows
the game speed and stops while paused. `render.c` offsets, turns and leans
the sprites (`strike_look()`), flashes the one struck by drawing it again
additively, and draws the marks after the sprites (`render_strike_marks()`,
`game/strike_marks.h`); a mark on a target killed by the blow shows where it
stood. `tools/preview_attacks.py` previews the timeline and the marks in a
GIF with the game's sprites (it mirrors both: change them together).

Blood on the ground (`core/blood.h`) is kept the same way: after the blows,
`bleed_on_ground()` lets every settler and animal with injuries bleed
(`blood_bleed()`): a drop or two on its steps while it walks (every
`BLOOD_STEP_TICKS`, out of step by slot), a pool fed while it stays still.
`Game.blood` is a ring of the newest `BLOOD_STAIN_MAX` stains with a random
stream of its own (`blood_rng`); no simulation code reads it and
`game_hash()` leaves it out. `render_blood()` draws the stains after the
ground (dry and fading by `blood_look()`), where blood can lie now.
`tools/preview_blood.py` previews it in a GIF.

### Occupancy

Settlers and animals walk through one another but never stop on the same
tile (`core/occupancy.h`, `Game.occupancy`, out of `game_hash()` but part of
the simulation). Beings are numbered by slot (`BeingId`: settlers 1 to 64,
animals after them). Each holds a claim on the one tile it stops on: where it
stands, or where the path it walks ends.

- **Searches skip the tiles others hold.** During a being's turn,
  `Pathfinder.stop_filter` is `occupancy_stop_filter()`: every nearest search
  (`pathfinder_find_nearest*()`, `goal == NULL`) only stops on tiles nobody
  else holds, or held by someone yielding. A* to a known tile is not filtered
  (a stockpile cell, a bed, a stack): a conflict there is met on arrival.
- **Settling, after each turn** (`settle()` in `core/colonist.c` and
  `core/animal.c`): the being is counted on its tile (`occupancy_place()`),
  yields when idle or strolling, and claims its stop tile. A dead being holds
  nothing; one knocked down forces its tile (`occupancy_force()`: whoever
  held it makes way, unless that one lies there too: then the newcomer crawls
  to the nearest free tile within `OCCUPANCY_CRAWL_REACH`, `crawl_aside()`). A refused claim is tried again at every tile on the
  way; one step short of it (`walk()` never starts the last step onto a tile
  it does not hold), a being waits up to `OCCUPANCY_PATIENCE_TICKS`, then
  makes way: a player's move goes to the nearest free tile, a fight or a
  flight goes on from where it stands, anything else is given up (the being
  decides again, its searches skipping the taken tile). Standing on a tile
  someone else took (a busy being took it from a yielding one), it steps
  aside to the nearest free tile (`TASK_WANDER`; a fighter or one fleeing
  keeps at it and only walks there).
- **Yielding.** An idle or strolling being's tile goes to whoever needs it to
  do something; idle ones never take each other's tiles. Strolls and herd
  walks pick free goals.
- **Crossing.** `Occupancy.present` counts the beings on each tile now; a
  step onto a tile someone else is on goes at `OCCUPANCY_CROSSING_SPEED`.
  Nobody ever blocks a path: a corridor one tile wide stays open.
- **Fighting** (`docs/COMBAT.md` §4): an attacker heads for a free tile from
  which its blow reaches its enemy (`combat_is_strike_spot()`, searched in the
  box around it), never the enemy's own tile; in reach on its way but on a
  tile someone else holds, it strikes in passing and goes on to a free one
  (it stops only where it may); with no room it waits and looks
  again every `COMBAT_SURROUND_RETRY_TICKS` (an animal gives up after
  `ANIMAL_SURROUND_PATIENCE` tries). `combat_can_strike()` is the reach test
  for strikes and their resolution: same level, in reach, and not diagonally
  across a wall's corner.
- **Eating, for animals**: one eats on the food's tile; when someone busy
  holds it, the others eat from a tile beside it (`is_food()`, `is_carcass()`
  accept a free tile with a direct step to such food; `Animal.target` is the
  food's tile, the path ends beside it). Settlers keep eating on the tile of
  the stack they reserved.
- `game_tick()` sweeps the occupancy before the turns (`sweep_occupancy()`):
  beings removed or dead hold nothing, newcomers are counted where they
  stand. A dead animal becomes a carcass anyone walks over.

### Journal

`Colonist.journal` and `Animal.journal` (`core/journal.h`) are rings of
`JOURNAL_MAX` entries of facts: an event (thought, wound, chat, chat about a
knowledge), a thought type or an attack, the body plan, part and injury of a
wound, who dealt it (a settler's name copied, or the animal's species) and
the tick. No text lives in the core. Thoughts come through `Mood.news`
(a bit per thought, set by `mood_remember()` and when a state begins, taken by
`mood_take_news()`): at the end of `game_tick()` each settler's news go to its
journal. Every blow is noted by `resolve_strikes()` in both journals: the
attacker's gets a `JOURNAL_STRIKE` (how it went: `JournalBlow`, and where it
landed), the struck one's a `JOURNAL_WOUND` when it was hurt, else a
`JOURNAL_STRUCK` (missed, dodged, harmless). Animals are noted by species and
sex (`JournalParty`). The same miss again within `JOURNAL_REPEAT_TICKS` adds
to the entry before (`JournalEntry.repeats`) instead of filling the ring. An
entry may be of several kinds (`journal_event_kinds()`: a wound from a blow is
a wound and an attack) and shows when one of them does. A chat is noted by
`conversation_hold()` in both. Nothing reads a
journal and `game_hash()` leaves it out. Wounds with no author (a fall, a
cave-in, a felled tree, the debug blow) go through `Health.notes`:
`health_hurt()` notes each wound (part, kind, `Health.hurt_cause`, which the
caller sets: `health_fall()` does for falls, `hurt_beings_at()` for the
others) until `health_take_notes()`; the game empties them into the journal
(`journal_note_hurts()`) right after the blow, or once a tick before the
strikes for what a settler's own turn dealt (`note_pending_hurts()`). The
notes of a strike are dropped: it is noted with its attacker.

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
job, one priority level at a time (1 first): a search for the nearest
passable tile next to an open job (A* towards the box of the orders or of
the gathering zones, `pathfinder_find_nearest_in_box()`) (designated, work type at that level, not
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
Resources belong to groups (`GatherGroup`: wood, food, fibres), each with an order of its own (`GatherGroupOrder`, always on: food
until 5 days of food by default, the others forever): a resource is wanted
only when its own order and its group's both want it, and a group's
`done` counts what its resources gave. An order may wait for the full harvest
(`ripe_only`): what plants bear (food, grass) only from plants whose stock is
full (every fruit ripe, not a grown plant), and trees, grass and roots (what
is taken whole, `PLANT_HARVEST_RESETS`) fully grown (on by default for wood
only), so that settlers do not walk to plants that give little. `gather_zones_update()` recomputes each tick which resources every zone wants
and bumps the zones revision when it changes; it is also bumped once per
in-game hour, after every plant has grown, so that idle settlers look again.

A zone tile offers a **job** (`GatherJob`: nuts, harvest, harvest carefully,
dig, dig carefully, cut grass, chop, in that order of preference on one tile:
the nuts lying there first, felling last) when it holds something a wanted
resource comes from. What lies on the ground (stones, branches, twigs) is no
gathering: those are ordinary items, hauled like any (stockpile filters
decide). Each wild food is a resource of the job that suits its plant: picked or
dug up, and "carefully" (Gathering 60, `GATHER_CAREFUL_MIN_LEVEL`) when easily
mistaken for a poisonous plant, so that the existing job masks decide who may
take it. Digging jobs (`GatherJobDef.fetches_tool`) send the gatherer to a
better digging tool lying within `COLONIST_TOOL_SWAP_MAX_COST` first
(`fetch_job_tool()`, the craft's fetch phase reused; orders that dig do the
same); it keeps it. Each job has its
work type (felling is woodcutting, the rest gathering) and knowledge; the
settler takes the first job of the tile among those it may do
(`Colonist.work_type` holds the job while `gathering`). The zone inspector
shows how many items of each resource the zone holds now
(`gather_zone_available()`: wood of the trees the orders allow, ripe fruit,
stocks, fallen nuts, grass), and warns about an
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
  about (`Reservations.releases_by_kind`). Such early retries also wait for
  the settler's own tick out of `COLONIST_SEARCH_STAGGER_TICKS` (8), counted
  by `Colonist.search_phase` (ticks lived, not the clock, which tests hold
  still): settlers whose searches failed together, idle when the last jobs
  went, would otherwise all search again on the tick something changes.

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
tile's plant. **Doors** open and close (`Map.doors`, cleared when a
structure changes): kept open by the player (`game_set_door_kept_open()`,
the tile panel; in the game hash, logged as a build change so rooms and
sight follow), or open while somebody stands in the doorway (`update_doors()`
every tick, from the beings' positions; only for the sight). An open door
lets the sight through (vision, animals' sight); a door kept open also lets
the light (`map_stops_light_at()`), the warmth (air for
`temperature_material()`) and the animals (`PATH_WALKER_ANIMAL`) through. Blueprints are **construction sites** (`core/construction.h`,
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
tick. The cancel tool removes sites (their materials are dropped), and so
does a new plan on a site in progress, which replaces it
(`construction_can_replace_structure()`, `game_place_structure()`; the very
same plan again changes nothing, `construction_is_planned()`); the
*take apart* order (`DESIGNATION_DECONSTRUCT`, construction work) removes a
built structure and gives back half of its materials.

Material counts come from real structures, for a tile of about 1 m²: see
`structure.c` and `docs/KNOWLEDGE.md` §10.

**Long structures**: a grass bed (and the ground bedding, a bed made of no material, resting as the bare ground does: it is not a "bed" for the mood memories) takes two tiles (`StructureDef.length`),
its head and its foot in one of four `Direction`s. Every tile holds the
type (movement cost, blocking and the inspector work per tile as before);
`Map.structure_part` holds the direction and the tile's place in the
structure, so `map_structure_origin()` finds the head from any tile. Quality
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

**Quality, hit points, wear**: two more map layers hold each built
structure's quality (`Map.structure_quality`) and hit points
(`Map.structure_hit_points`). `map_set_structure()` gives a standard quality
and full hit points (worldgen, tests); a finished site uses
`map_build_structure()` with the builder's quality
(`knowledge_quality_of_score()` of a roll at their level, through the
game's `Rng`), capped by `structure_quality_cap()`: the average quality of the
rated materials delivered (branches; grass and twigs do not cap) + 20, as
for crafts. `structure_max_hit_points()` scales the definition's hit points
by the quality (×0.6 at 0, ×1 at 50, ×1.5 at 100). Once a day (`game_tick()` on the
day boundary) `constructions_wear_day()` takes `wear_per_day` hit points from
structures that wear by themselves (a grass bed: 1); one at 0 falls apart
(with a refresh site on it, whose materials are dropped), one below
`STRUCTURE_REFRESH_BELOW` (half) gets a **refresh site**: a construction site
with `refresh` set, on the built structure, wanting `refresh_material` and
`refresh_work` instead of the building ones (`construction_material()`,
`construction_material_count()`, `construction_work_needed()`). Builders
handle it like any site; finishing it brings the hit points back to full and
keeps the quality. Taking the structure apart removes its refresh site.

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
was built as; `map_roof()`, `map_set_roof()`), with its quality in
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
(`TEMPERATURE_MATERIAL_OPEN`). `map_hash()` only hashes the quality layer once
a roof exists, so older hashes stay valid.

**Temperature** (`core/temperature.h`, `Game.temperatures`, part of
`game_hash()`): `temperature_outdoor()` is a function of the tick (seasons
blended with a cosine between their middles, `temperature_season_mean()`,
plus a daily swing coldest at 5:00 and warmest at 15:00,
`temperature_daily_swing()`); the weather changes it (`weather_outdoor()`,
below), and `temperatures_tick()` takes the result. Each tile holds a
temperature; every tick `temperatures_tick()` updates one band of rows in
place, a whole pass every `TEMPERATURE_SWEEP_TICKS` (200, 4.8 in-game
minutes). Heat flows between a tile and its four neighbors at the lower
conductance of the two (air 4/h, door 1.5, wall 0.5), toward the outdoor air
at a rate set by its cover (open sky 6/h, built roof 0.3, natural roof none;
the first floor above a tile decides) and, under a natural roof, toward the
ground (10 °C, 0.3/h). Rock stays at the ground's temperature and open space
under the sky is the outdoor air (`TEMPERATURE_MATERIAL_OPEN`): a side against
rock pulls a tile toward the ground, a side open to open space toward the air
(at the lower conductance), so only tiles the sweep works out share edges.
A pass works each tile out edge by edge with its east and south neighbors
(the west and north edges were those of the tiles before it): the edge's rate
times the pass is the share of their gap it closes, what one tile gains the
other loses (the heat is kept), and the step stays stable while that share is
at most one half (air: 0.32); then the tile closes `Temperatures.pull` of its
gap to the outdoor air and the ground (`ground_share` says how much of that is
the ground), at most all of it. The steady states are those of the heat
equation. The previous explicit step (rates times the time, every 100 ticks)
needed a tile's rates to add up to less than 25/h, and was some 2 °C off in a
heated hut and much more beside a fire; this one is about as close (closer
outdoors, about as far in a hut, on the warm side) with half the passes.
Burning hearths put their heat in through `temperatures_heat_source()`: it
waits for the pass over the tile, half of it added before the tile is worked
out and half after (it came in all along the pass), so the tile neither piles
up a pass's worth of heat in a saw-tooth nor sheds it unseen. The bands of rows
run through all the levels one after the other (`height × depth` rows),
and only the tiles of a bitset (`Temperatures.active`) are worked out: rock
and open space, most of a hilly map, are skipped 64 at a time. For now a
tile only exchanges heat with the tiles of its own level (the edges between
levels have a rate of 0). What each tile is made of and covered by, the
pulls and the rate on each edge are caches: updated for the tiles of
the map's change logs and the tiles below them, whose cover they may change
(`Map.topology_log`, and `Map.build_log` for structures, roofs and rock
where one could not walk anyway), rebuilt whole when the logs overflowed or
the layers were written directly (`MapTopologyChange.anywhere`). Edge rates
of 0 and a padded layer let tiles on the edges of the map go through the
same loop.

**Microclimate** (`docs/CLIMATE.md` §5): a tile under the sky is not pulled
toward the outdoor air itself (the air of a weather station's meadow) but
toward what its surface makes of it: warmer on bare dry ground in the sun,
cooler in the shade of crowns and where the sun goes into evaporation,
milder at night under crowns that hide the sky
(`TEMPERATURE_SUN_HEAT`, `TEMPERATURE_EVAP_COOLING`,
`TEMPERATURE_NIGHT_COOLING`), and more slowly under crowns (the air is still
there: `TEMPERATURE_CANOPY_STILL`). It then trades heat with the ground under
it (`Temperatures.soil`, faster and slower to follow under open water), which
stores the day's heat and gives it back at night. The game hands the pass a
`TemperatureSurface` every tick (`temperatures_set_surface()`): the light
let through and the share of the sun gone into evaporation of each tile, from
`Game.soil`, and the sun and night factors; the pass works the factors out
once (`SurfacePass`). Without a surface (tests, a map with no soil) the open
sky pulls toward the outdoor air as before. `game_settle_climate()` sets the
air and ground to the outdoor and season's temperatures after the clock
jumps.

**Soil water** (`core/soil.h`, `Game.soil`, part of `game_hash()`;
`docs/CLIMATE.md` §3): each tile of rooting ground holds water in mm in a
bucket set by its terrain (`SoilDef`: wilting point, field capacity,
saturation, drainage, intake). It is worked out once an hour in slices over
the ticks like the plants (`soil_update()`, from `update_plants()`), with
what the hour brings worked out once for the map (`SoilHour`: rain from the
weather's `rain_mm`, the snowpack's melt, the potential evapotranspiration
from the sun, the sky, the temperature and the air's humidity, the dew
point): rain lands less under crowns and not at all under roofs or rock,
water above field capacity drains, the crowns over a tile, the plant on it
(`PlantDef.transpiration`) and its bare soil take water by what light each
gets, dew condenses on cold clear nights, and the water table keeps the
ground near open water wet (`Soil.wet`: the distance to water, rebuilt when
`Map.water_revision` changes). What covers each tile (`canopy`) is looked up
every `SOIL_CANOPY_HOURS`. The moisture (`soil_moisture()`) is one scale for
every terrain: 0 at the wilting point, 1 at field capacity, 2 saturated, 3 in
open water; the tile inspector shows it in words (`soil_moisture_key()`).
Plants feel it (`docs/CLIMATE.md` §6): `game_plant_conditions()` passes it in
`PlantConditions.moisture`, and each plant's `PlantDef.water` (`PlantWater`)
gives its growth a plateau of moisture and sets when it dries out or its
roots drown (`plant_water_response()`, health worn down in
`plant_advance()`); spreading skips ground too dry or too wet, and world
generation places plants by the moisture their ground is expected to hold.

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
(`render_temperature()`, key H). Two more views take the same place, one
at a time (`app_toggle_*_view()`): the light on each tile (`render_light()`,
key G) and the water in its ground (`render_moisture()`, key K, from
`soil_moisture()`, rooting ground only); the status panel reads the tile
under the mouse in the view's terms, and always shows the air's humidity
(`weather_outdoor_humidity()`).

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
(crafting spot, `workstation_needs_construction()` false) is placed ready at
once; a structure with no material and no work (ground bedding,
`structure_needs_construction()` false) is built at once likewise
(`game_place_structure()` returns `CONSTRUCTION_BUILT_AT_ONCE`, no site). The
plants under them are left alone and ordered uprooted (`DESIGNATION_UPROOT`,
`order_uprooting()`), a settler does it. They are drawn as an outline only,
since nothing was built. A knapping spot or a hearth (six cobbles) is a site
as before, its plant cleared first. `game_can_place_workstation()` keeps room in the station list for
every site (`Constructions.workstation_count`); should a station still not
fit when its work is done (a tree grown on the tile), the site waits. The
cancel tool removes blueprints; removing a station drops the input of a
craft in progress.

A built station carries up to `WORKSTATION_BILL_MAX` **bills**: a recipe
(`core/recipe.h`), a mode (do N times, until N in stock, forever), the input
types allowed, how far inputs are searched for (`input_radius`, a travel cost
from the station, 80 by default), who may work on it (`BillWorker`: anyone,
settlers whose level in the recipe's knowledge lies in `min_level`..`max_level`
(inclusive, 0..100 by default: beginners for cheap things, masters for the
best ones), or one settler, `Bill.colonist`, a generational handle that no
longer matches anyone once the settler is gone), the quality range of its
ingredients (`min_quality`..`max_quality`, every quality by default) and a
pause flag. The worker fields are hashed only when the bill is not for
anyone, the quality range only when it is not the whole range.

Ingredients are chosen item by item (`ItemChoice`, `core/item.h`): a heap
counts only for its items within the bill's range (and the recipe's least
quality), and only those are picked up (`items_pick_up_chosen()`). For food
and other things that rot, the settler first looks around the station (the
bill's radius, the station's level and the next ones) for the ingredient that
rots away soonest (`items_soonest_rot_hours()`), then fetches from the nearest
heap holding one that rots within six hours of it, and picks up the soonest
to rot first; when none of those can be reached, any heap in range will do.

"Until N" counts `Game.stock`: items lying in a stockpile that accepts them,
plus those on their way there, lying on a workstation tile (where products
are dropped) or carried by a hauler. Counting only stored items would let a
crafter make more while the last ones wait for a hauler; counting every item
on the map (as gathering zones do) would count tools lost in the wild.
`game_update_stock()` scans the stockpiles (their bounding boxes), the
stations and the colonists; `game_tick()` only calls it while some bill is in
"until N" mode (it costs as much as the rest of a small colony's tick). It
also keeps where the stock lies (`stock_stored`, `stock_on_stations`,
`stock_hauled`, adding up to `stock`) for the resources list, which
refreshes it every frame.

Bills are worked through from the top: a crafter takes the first one that
is not paused, wants more, whose recipe it knows at a level the bill accepts
(`colonist_fits_bill()`), whose tools can be had (carried, or lying
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
worker's level, capped by the material (`recipe_quality_cap()`: the material's quality + 20,
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
quality factor (×0.8 at 0 to ×1.2 at 100). Every tick of work multiplies the
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

Workstations and construction sites have a **priority** of their own
(`Workstation.priority`, `ConstructionSite.priority`: `JOB_PRIORITY_HIGHEST`
1 to `JOB_PRIORITY_LOWEST` 10, default 5, like stockpiles and gathering
zones). Within one work type, crafting, building and supplying take the
nearest job among the stations or sites of the best priority that has one:
a bit mask of the priorities present (`station_job_priorities()`,
`site_job_priorities()`), then one search per level present, best first, so
that with every priority at its default there is a single search as before.
A site's priority goes to the station built from it. Both are hashed only
when not the default, so hashes of games that never set one are unchanged.

### Hearths and fire

An open hearth (`WORKSTATION_OPEN_HEARTH`, `WorkstationDef.hearth`) is a
workstation built like a site (six cobbles, 250 work, Fire 50) and walked over
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
  reserves the hearth (`RESERVATION_FIRE`) and goes through `LightStep`s.
  It is only taken up while what is to be laid lies somewhere. First the
  fire is laid (`HearthIngredient`, `Workstation.laid` bits): a handful of
  dry twigs as kindling, then a branch, each fetched and laid on its own
  (`LIGHT_FETCH_LAY`, `LIGHT_BRING_LAY`, `hearth_lay()`: its weight goes to
  the fuel). Wet twigs (`ITEM_WET_TWIGS`) do not count as kindling: a
  felled tree's are green, dry twigs lying in the rain get wet
  (`ItemDef.wets_into`, the twigs the trees shed too), and wet ones dry in
  a day like fresh grass. `hearth_light()` clears `laid`:
  every lighting lays the fire again. Then, below `HEARTH_LIGHT_FUEL_MIN`
  in it and on its way (`hearth_wants_fuel_to_light()`) with some fuel the
  hearth takes lying somewhere, fuel fetched and put in
  (`LIGHT_FETCH_FUEL`, `LIGHT_BRING_FUEL`: no hauler needed). Then embers
  from another burning hearth within `HEARTH_FIRE_CARRY_MAX_COST`
  of it (`Colonist.ember_ticks`, a flag rather than an item: they last
  4 hours and are dropped with the task), else a handful of grass fetched as
  tinder and an attempt by friction, which burns the tinder and succeeds by
  `hearth_friction_chance()` of the level before the attempt (an RNG draw).
- **The Fire tab** lists what lighting takes while the hearth is out:
  tinder, kindling and wood for the flame, "in place" once laid, red with
  the bill panels' "no suitable material" tooltip when
  `game_hearth_lacks()` (none lying in the colony; embers from another
  burning fire stand for tinder).
- **Recipes that need fire** (`recipe_needs_fire()`: made at a hearth) are
  skipped by `choose_bill()` while the hearth is out, and a craft stops when
  it goes out (the input and progress stay on the station). Hardening keeps
  the spear's quality and hit point percentage (`recipe_craft()`).
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
- **Move orders**: `game_order_move()` (the Move tool: a left click on the map with settlers
  selected) gives each a goal of its own near the spot (the first gets the
  spot, the others the nearest free tiles within 4; unexplored tiles and tiles
  not reachable on the known map are left out) through `colonist_order_move()`:
  the job is given up (`begin_task()` releases reservations and puts down what
  is carried), the path is found on `plan_map`, and the settler walks it as
  `TASK_MOVE`, stands `COLONIST_MOVE_HOLD_TICKS` (5 s) at the end, then goes
  idle. Between two tiles the order waits for the next one
  (`Colonist.move_pending`). `decide()` leaves a `TASK_MOVE` alone except for
  a break, critical hunger or thirst, or critical tiredness.
- **Heat**: a burning hearth adds `HEARTH_HEAT_PER_HOUR` degrees to its
  tile every tick (`temperatures_add_heat()`, see below).
- **Rendering**: a ring of stones, sticks for the fuel, flames moving with
  real time; the fire's light (`core/fire_light.h`: linear over 6 tiles of
  3D distance, along a ray from the flame that walls, floors and rock stop)
  is one of the two lights `render_daylight()` draws, with a warm glow where
  it is dark (see *Light and shade*).

**Leaves** (`Map.bare`, docs/PLANTS.md §12): the crowns of deciduous trees
go bare in winter. The game derives one number from the climates of the
trees (`leaf_conclude()`, once per hourly pass over the plants), the core
lets bare foliage through more light in `map_light_cover()` (the light cache
follows `Map.light_revision`), and the drawing shows bare-branch textures with
the leaves fading over them.

**Light and shade** (`render_daylight()`, presentation only): the light of a
point is the sky (the sun's day with its dawn and dusk, `game_daylight()`, a
night sky `NIGHT_SKY_LIT`, the weather, `weather_light()`), split into the
light of the whole sky and the direct sun (`shadow_direct_share()`: none
under clouds, fog or rain, fading out with a low sun), put together by
`shadow_light()`: the night sky and the sky's diffuse light times how much
of the sky the surface sees (`core/sky.h`: rays over the dome through
blocks, floors and foliage, so a closed hut is dark at noon, a roof on posts
bright under it, a cave darkens from its mouth, the foot of a cliff sees
half the sky), the direct sun times how much of it reaches the point (the
shadows, below); the light of burning hearths on top (`core/fire_light.h`:
per point, along a ray from the flame, so a fire lights its room and falls
out of an open door). The point is then as
dark as `1 - (LIGHT_FLOOR + (1 - LIGHT_FLOOR) × light)`, `LIGHT_FLOOR` being
`GAME_DAYLIGHT_MIN`, the darkest it ever was. The shade is a veil drawn over
the sprites (colonists, animals, plants and items are in the shade of their
spot) from a texture with several points per tile (8, 4, 2 or 1 a side as
the view zooms out), stretched over the tiles with smoothing; the light of
the sky is a value per tile corner (the mean of the floors around it, of
the blocks where no floor touches it), blended across the tile, the shadows
and the fires are per point. Its color is cool by day, the night's blue by
night, a touch warm at dawn and dusk. The light of the sky is kept per
column of the level shown (`SkyField`) and worked out again, within a time
budget per frame, where `Map.light_stamps` changed within `SKY_REACH`;
the light of each fire is kept for its points until the fire, the level
shown or the map within its reach changes (`FireLight`, a budget of rays per
frame). The simulation still reads `map_light_cover()` (docs/SHADOWS.md
step 5).

**Shadows on screen** (`core/shadow.h`, docs/SHADOWS.md §4): the sun's rays
(`sun_lit()`) from the surface drawn on each point (the floor seen, or the
top of a block: `shadow_surface_height()`), worked out from the map on
screen (the known world under the fog) ahead of time, by pieces of 16 × 16
tiles and half hours of game time: for each point how lit it is at the
start, middle and end of the half hour, and the moment the edge of a shadow
passes between two that differ (found by halving). Frames cast no ray: they
read the value before or after each point's moment for the time now,
blended a little around it, so the shadows glide at any speed and stand
still when paused. The frontend gives the work a time budget per frame
(more while pieces in view have nothing to show); the next half hour is
worked out ahead. A piece is out of date when the map changed within the
reach of its rays: `Map.light_stamps` holds, per block of 16 × 16 columns,
the light revision of its last change (terrain, rock, a wall or a door, a
tile copied into the known world, the crowns going bare everywhere), and a
piece compares the blocks towards the sun as far as its rays go. A piece keeps the surface each column was worked out for
(`shadow_surface_code()`, its height), not the level shown: showing another
level works out again only the columns whose surface differs (most of the
ground shows through the open space above it), the old values shown
meanwhile; the light of the sky (`SkyField`) does the same per column.

Stones, branches and twigs lying about are laid by world generation in map
layers (`Map.cobbles`, `Map.flint`, `Map.branches`, `Map.twigs`); `game_init()`
turns them into ordinary items (`map_litter_to_items()`,
`items_add_natural()`, docs/ITEMS.md), each with its own quality, condition,
spot and angle, and sets `Map.litter`: from then on `map_stones()`,
`map_litter()`, `map_add_litter()` and the others read and change those
items (twigs counting wet ones), so trees shed items. They are items like
any other: in the stacks and `Items.totals`, hauled, forbidden, moved by
hand, used by bills and builders where they lie; nothing is gathered from
the ground, and nothing crumbles away (the trees' shedding stops at
`MAP_BRANCHES_MAX` and `MAP_TWIGS_MAX` a tile, wet twigs counted). At most
`MAP_STONES_MAX` (20) stones lie on a tile, never growing back. World
generation draws 0 to 20 stones for
every stony tile (`TerrainDef.stony`: soil, rich soil, gravel, rough stone; not
sand), from a random stream of their own so that the plants do not depend on
them; on flint patches (noise, denser on gravel and rough stone) each stone is
flint with the patch's share, up to 60 %, and of the others only one in 50 is
kept as a cobble (about 4 800 on a 200 × 200 map). The spawn makes sure a tile with at
least `GAME_SPAWN_FLINT_STONES` flint lies near the settlers, laying a small
patch otherwise.

### Forbidding

The player may forbid the settlers things in the colony; each flag lives
with the owner of the thing, so that it goes when the thing goes:
`Map.forbidden` (`MapForbid` bits: the plant of a tile, a structure on its
head tile, a roof; cleared by `set_tile_structure()`, `map_set_roof()` and
`map_remove_plant()`, the only way plants are taken away once a game runs),
`Item.forbidden` (per item, so it goes with the item wherever it is carried;
`Items.forbidden` sums it up per tile: some or all of its items forbidden) and
`Workstation.forbidden`. `game_set_forbidden()` (`ForbidTarget`) resolves a
tile of a tree's trunk or foliage to its foot; `game_set_forbidden_rect()`
(the Forbid and Allow tools) takes everything on a rectangle, grass included
(it is a plant like the others), but not zones, which go as a whole.

Settlers see forbidden things as absent rather than through checks
scattered over every job. Forbidden items never share a group with allowed
ones (`ItemStack.forbidden` is part of the group, as the dead one of a body
is): settlers read the items through `items_get_allowed()` and
`items_get_allowed_of()` (the newest group of allowed items) and pick them up
with `items_pick_up_of()` and `items_pick_up_chosen()` (allowed items only),
so pickups, hauls, deliveries, meals and fuel skip them, beside allowed items
of the same tile; `items_drop()` and haul destinations never add to a tile
holding forbidden items (`items_any_forbidden()`). The player forbids one
item (`game_set_item_forbidden()`, from its window) or every item of a tile
(`FORBID_ITEMS`, the tile's items window and the Forbid tool); `map_has_edible_plant()`, `covered_tile_holds()` (gathering),
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

Plans and zones are forbidden as a whole, with a flag of their own:
`ConstructionSite.forbidden` (`FORBID_SITE`: `construction_forbidden()` is
true, so no material is brought and no work done, and `finish_constructions_now()`
leaves it), `Stockpile.forbidden` (`FORBID_STOCKPILE`: nothing is hauled onto
its tiles, `haul_destination_room()` and `storage_levels()`, nor off them,
`is_haul_source()` and `is_extra_source()`; what lies there still counts as
stored) and `GatherZone.forbidden` (`FORBID_GATHER_ZONE`: the zone wants
nothing, `gather_zone_wanted()`, so every gathering search skips it while its
orders stay). From any tile of the zone or plan, `game_set_forbidden()` takes
the whole. The forbid rectangle takes plans in and leaves zones out. The flags
enter the hashes only when set. The frontend draws the "no entry" sign on a
forbidden plan's head and every third tile of a forbidden zone (all of a
small one).

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
  quality) and does all the work at `GAME_DEBUG_BUILD_LEVEL` without drawing
  from the RNG (average quality). Zones leave the tiles it built on, as after a
  builder's work. A roof without support and a station that cannot be placed
  wait; sites are finished in list order, so walls placed with their roof
  hold it up in the same tick when they come first.
- **Free crafts**: `ColonistContext.free_crafts` lets `choose_bill()` skip
  the input check (`bill_free_input()`: the bill must allow some input type)
  and `plan_station_job()` skip fetching it; at the station,
  `workstation_start_free_craft()` starts the craft with the recipe's count of
  the first allowed input type, standard quality at least. `Workstation.free_input`
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
  last two where the player clicks, and `game_debug_spawn_animal()` (a wild
  animal of the species chosen in the menu, a male with Shift, on the ground
  seen under the cursor; it joins the herd of the nearest animal of its
  species and sex close by).
- **From the command line**: `--debug instant_build,learn_all,...` (or
  `all`) turns cheats on from the start, `--scenario <n>` sets up a test
  situation of `core/scenario.h` (the benchmark's), `--colonists <n>` sets
  the number of settlers, `--animals <percent>` the number of wild animals
  (0: none): a visual check starts where it needs to, without clicking its
  way there.

### Vision

Designed in `docs/VISION.md`; `core/vision.h` is step 1 (sight). The decision
that shapes the rest: settlers' choices (paths, regions, every search) will
read a **known world**, a second copy of the map's layers and of those of
items, structures and sites, of the same types, where unexplored tiles have an
unknown terrain; the simulation keeps writing the real map, and changes to
tiles someone sees are copied over. With the See everything cheat (the
default of tests and benchmarks not about vision) the known world is the real
map, so behavior and state hashes stay those of today.

`Vision` (owned by `Game`, built by `game_set_vision()` or by the fog, and
updated at the end of each tick while `Game.vision_on`) is derived from the map and the settlers'
positions, so it is not part of `game_hash()`:

- **A field** is a bit per tile (`map_index()` order) over the tiles with
  something in them ("tracked": rock, a floor or a structure; open space is
  left out), so that a field of plains is its ground and not the air over it.
  `vision_cast()` makes one for a tile (with the eyes leaning when a wall is
  next to it, a sleeper's being the nine tiles around it), `vision_cast_eye()`
  for a single eye.
- **A cast** shoots one ray per tile of the border of the region looked at (the
  whole map, or a square of `radius` tiles), two per tile near the eye
  (`RAYS_PER_TILE_NEAR`) and one over the map. A ray goes column by column
  along the grid (an Amanatides and Woo walk) and is a problem in its own
  vertical plane: it keeps the intervals of slopes (height over distance)
  still open, and each column looks at the levels the open slopes can reach
  there, going up through the levels for the slopes above 0 and down for those
  below. A solid tile (rock, a wall, a door) is seen when an open slope
  reaches its box and takes those slopes off the set; any other tile is seen
  when one reaches its floor, a plane at the bottom of the tile that stops the
  ray too, unless it is thin foliage. `Vision.column_top` (1 + the highest
  tracked level of each column, kept right from the map's change logs and a
  slow sweep for what they miss) keeps columns from looking at the air above
  them; the ray also ends when its set is empty or beyond what is left to see.
  A set that would split in more than `OPEN_MAX` pieces is left open
  (permissive, not wrong). Nothing calls a math function but `sqrtf()`, which
  is exact.
- **Viewers** (`vision_viewer_set()`, one per settler slot) own a field. Their
  near square is cast again on every step, posture change or nearby map change
  and replaces what the field held there (`replace_bits()` updates
  `Vision.seen`, the number of viewers seeing each tile, from the difference);
  the whole field every `VISION_FAR_INTERVAL` ticks or after
  `VISION_FAR_MOVE` tiles of walking. A viewer may have a `limit` in tiles
  (`vision_viewer_set_limited()`: a settler lying collapsed); both fields are
  cast again when it changes.
- **Explored tiles** (`Vision.explored`, a bit per tile: seen by anyone ever,
  or marked) and the tiles in view now (`seen_bits`) are kept by the same
  bookkeeping as the counts; `Vision.snapshot` collects the tiles that came into
  view, went out of it or were explored without being seen, for the known
  world to copy. `vision_explored_hash()` goes into `game_hash()` while the
  fog is on.
- **The known world** (`core/known.h`, `Game.known`): pictures of the map, the
  items and the dirt of the same types and sizes (all open space at first),
  and per tile what a workstation there looked like (`KnownStation`: type,
  fire, fuel, what lies on it; workstations can be destroyed, unlike the
  player's zones, orders and blueprints, which are plans and always known), with `map_copy_tile()`, `items_copy_tile()` and `dirt_copy_tile()` keeping
  their counters right. `sync_known()` (game.c), at the end of each tick,
  copies the snapshot tiles, and a twelfth of the tiles in view (the
  `KNOWN_SYNC_SLICES` slices), so that a tile out of view holds what was
  seen last. `game_view_map()`, `game_view_items()` and `game_view_dirt()`
  give the pictures under the fog and the real ones without: the renderer,
  the tile readout, the tile inspector and the resources list read them.
  Nothing in the simulation does yet.
- **The fog** (`game_set_fog()`; `Game.fog_on`, off by default so that tests
  and benchmarks are as before; the application turns it on unless
  `--debug see_everything`): needs the vision, allocates the known world,
  explores the start once (`core/exploration.h`: tiles within 14 steps of
  the arrival point, what is seen from it, and what is seen along a walk from
  a random edge, drawn from a `Rng` of its own) and copies everything explored.
  `game_reveal_map()` explores every tile. Tools are refused on unexplored
  tiles in the core (`game_designate_rect()`, `game_set_forbidden()`,
  `game_can_place_structure()`, `game_can_place_workstation()`); zones go
  through the known world (`game_add_stockpile_rect()`, `game_add_gather_zone_rect()`).
  `render_fog()` veils the tiles seen before and not in view.
- **Settlers plan on the known world** (`ColonistContext.plan_map`,
  `plan_items`, set by `colonist_context()`: the known world under the fog,
  the real one without): every path and every search (`pathfinder_find*()` in
  colonist.c, the food and fetch filters, the hauling world) reads them, so a
  tile nobody has seen is open space that nobody walks to or finds anything
  on, and a remembered tile is as it was last seen. What a settler does
  (picking up, eating, mining) is done on the real map and items, so it finds
  out what changed when it gets there (a path blocked meanwhile is given up,
  food gone is looked for again). A target checked while walking (food, a
  stack to haul) is only checked against the real world while the tile is in
  view (`items_checked()`, `map_checked()`), else against what is known.
  Connectivity regions are still the real ones: they only decide where a
  pre-check looks, and say "unreachable" at most too early. Items are
  mirrored: `Items.mirror` (the known items) and `mirror_when` (the tiles in
  view) make `items_add()`, `items_remove()`, `items_set_forbidden()` and rot
  copy a changed stack into the known world at once where it is in view, so a
  settler that has just picked something up does not plan on the stale stack
  (before this, settlers re-fetched what they had just taken, 570,000 times in
  two days of the benchmark). The rest of what is in view is copied a 48th at
  a time (`KNOWN_SYNC_SLICES`), in runs of consecutive tiles
  (`known_copy_run()`).
- **Cost of the fog** (`bench_simulation ... fog-revealed`, 12 settlers,
  release): about 28 µs a tick on average against 8 µs without, a tick in a
  hundred at 3 ms (a whole field cast, 2 ms). Limits: `VISION_NEAR_PER_TICK`
  near casts and `VISION_FAR_PER_TICK` whole ones a tick (`vision_limit_casts()`,
  turned on by the game: a settler that does not get its turn keeps its field
  and the turns rotate), a near field (12 tiles) recast after 2 tiles walked.
  With `fog` instead the settlers also search in vain for what they have not
  found yet.
- **Cost** (`bench/bench_vision`, release, 200 × 200 × 18 world): about 2 ms
  for a whole cast (3.8 ms the slowest of 100), 0.3 ms for a near cast. Most of
  a whole cast is rays crossing open ground to the edge of the map (a tile a
  step, about 30 ns); the leaning eyes' short range halves it. Both are
  tuned in step 5, with `bench_simulation` measuring the game.
- **Tested** (`tests/test_vision.c`): hand-built scenes, the counts against
  the fields, and generated worlds against a brute-force line of sight (27
  sample points per tile and a 3D grid walk): the rays miss about 2 % of
  what the brute force sees near the eye and 6 % over the map, and add 4 %
  and 6 % at the edges of shadows.

## Pathfinding

- 8-directional grid, cost of entering a tile = `map_move_cost()` (× √2
  diagonally), no diagonal move past an impassable corner.
- A* with the octile heuristic scaled by `PATH_MIN_TILE_COST` (admissible as
  long as no passable tile is cheaper: checked by a test).
- `pathfinder_find_nearest()` runs Dijkstra until a predicate matches (food,
  free tile...), with a cost limit to bound the search.
- `pathfinder_find_nearest_in_box()` does the same for a predicate true only
  inside a `TileBox` (tiles next to orders or zone tiles): A* with the
  distance to the box as heuristic (consistent, since every step between
  levels costs at least `MAP_CLIMB_COST` tiles), so a far search heads for
  the box instead of flooding a disk around the start. The cost found is
  the same as Dijkstra's; among spots or paths of equal cost the one taken
  may differ.
- A walk to a known tile (a settler's own bed) is a plain A*, not a search
  for a predicate true on that tile only.
- `Pathfinder.stop_filter`: when set, nearest searches only stop on tiles it
  accepts besides their predicate (the occupancy sets it around each being's
  turn: tiles others hold are skipped, see *Occupancy*).
- A `Pathfinder` owns per-tile scratch nodes reused across searches (stamps
  instead of clearing), so searches do not allocate. The fields of a tile
  (`PathNode`: stamps, cost, parent) sit side by side: one cache miss per
  tile looked at.
- Slopes and ladders lead to the levels above and below (orthogonal steps
  only, ×2 and ×3: see *World representation*); the heuristic adds the
  least those climbs cost (`MAP_CLIMB_COST - 1` per level), so it stays
  admissible (a test compares
  A* with Dijkstra on hilly maps).
- Walkers: `Pathfinder.walker` says who the next searches are for. Settlers
  (the default) go everywhere; animals (`PATH_WALKER_ANIMAL`, set around
  their searches by `core/animal.c` and put back) open no door and climb
  neither ladders nor trees. `path_walker_can_stand()` and
  `path_walker_can_step()` answer the same questions for a single tile or
  step. Connectivity regions are the settlers': for an animal they can say
  "maybe" where the answer is no, never the reverse.
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
- Each region keeps a box holding its tiles (`regions_box()`, grown as tiles
  join, made again on a rebuild); designations and gathering zones keep one
  for their tiles (`Designations.box`, `GatherZones.box`, emptied with the last
  tile). The pre-check scans below only go over the intersection of these
  boxes: the map has 18 levels, mostly rock or open air, which they used to
  read through on every search.
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

`bench/bench_simulation [colonists] [days] [scenario] [size] [seed] [flat] [fog] [wildlife]` runs a
colony without a window (scenarios: `core/scenario.h`, which the game can
also start on with `--scenario`) and prints the cost of a tick in processor
time of the thread (steady on a busy machine, where the wall clock counts the
time other programs ran): average, percentiles, the slowest ticks with their
number and in-game time, then how often the connectivity regions were rebuilt
and the final state hash. The simulation being deterministic, a slow tick can
be looked at under perf: with `BENCH_TIMESTAMPS=1` and `perf record -g -k
CLOCK_MONOTONIC`, the benchmark prints the time range of each slowest tick for
`perf report --time`. The `profile` preset (optimized, debug symbols, frame
pointers) gives readable call stacks. Compare a baseline build and the change
one after the other on the same machine: the numbers drift by 10 % or more
from one session to the next.

The game has its own: `--bench-render <frames>` draws that many frames from
a few fixed views (the start level, 1, 5 and 10 levels up, then zoomed out),
the simulation paused and without vsync, prints the time of a frame and of
its drawing (the draw calls carried out, the wait for the screen left out),
then quits; `--no-vsync` uncaps the frame rate, and the debug overlay (F3)
shows the drawing time per frame.

In November 2026 the box scans above took scenario 3 from 49 to 39 µs per
tick and scenario 1 from 34 to 26 (64 colonists, 10 days, same hashes); the
slowest ticks (60 ms in scenario 3) are single searches of a gathering zone
that succeed far away, left as they are. An optimization that is not supposed to change behavior
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

- SDL3 2D renderer (`SDL_Renderer`), VSync on (off with `--no-vsync` and `--bench-render`), alpha blending enabled.
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
  changing it and commit the PNG files. Some sprites and ground textures are
  drawn instead by a script of their own in `tools/assets/sprites/` (SVG
  rasterised by librsvg, or numpy and Pillow), tuned in
  `tools/sprite_workbench.py`: a window that runs a script with its top-level
  constants (its knobs) changed, catching the pictures it would write, shows
  a sprite on a ground of the game as the game anchors it (a ground texture
  repeated over the tiles), and saves the PNG (a ground texture with its
  wrapped border) or writes the knobs back into the script. At startup they
  are packed into one
  atlas (shelf packing, sizes and places multiples of 32 with 32-pixel gutters),
  and five halved copies are computed (opacity-weighted averages; SDL has no
  mipmaps). A batch uses the smallest copy at least as detailed as the
  screen, so trees do not shimmer zoomed out. Sprites are queued as textured
  quads and drawn with `SDL_RenderGeometry()`; the vertex color tints them
  (hair color) or fades them.
- A sprite is anchored by the middle of its bottom edge on a point of the
  world (the bottom middle of a plant's tile, a colonist's feet) and may
  overflow its tile: grown brambles reach 2 tiles high and 1.5 wide. Plants and
  colonists are drawn together from the back (top of the screen) to the
  front (`render_world_sprites()`): rows of plants merged with colonists
  sorted by the depth of their feet. A plant (a bramble, a sapling) that
  would hide a colonist standing behind it is drawn see-through.
- Trees that take room are drawn like walls and floors, no sprite: a trunk
  tile shows its cut face (wood with growth rings on a 1 × 1 trunk, bark on
  the sides that face out, `render_trunk()`), a foliage tile a floor of
  leaf clumps (`render_foliage()`), and the ground under foliage is shaded
  (`under_foliage()`). Viewed from above, the levels below are blurred as
  usual.
- Plants: a table in `sprites.c` gives each plant type its sprites: trees and
  hazels (seedlings, the first two stages only), brambles one per growth
  stage (`plant_growth_stage()`, 5
  stages), low plants three sizes the stages are shared out among, mushrooms
  one; brambles have a second set showing their food
  (`plant_has_food()`). Withered plants are not drawn. Sprites are mirrored
  on some tiles for variety.
- Colonists: four layers drawn in order on the same 128 × 192 canvas: body
  (naked, without arms nor legs; thin, average or heavy, per skin tone), head
  (per skin tone and expression), beard, hair. Clothes will be extra layers
  between the body and the head.
  Hair and beard sprites are light gray, tinted with the hair color. The
  expression comes from the needs (`colonist_expression()`). Every layer
  comes in three views (`SpriteView`): from the front, from the side (facing
  right, mirrored for the left) and from behind, where a head shows no
  expression (one sprite per skin) and a beard is not drawn.
- Views: a walker is seen the way it heads (`colonist_heading()`,
  `animal_heading()`, from the step it is on: `path_step_heading()`): from
  the front going down the screen, from behind going up, from the side as
  soon as it moves sideways (diagonals too). A settler that stands, works or
  sleeps faces the viewer. Animals stand, eat and sleep side on (the side
  they last turned to) and have a standing sprite from the front and from
  behind for walking (`ANIMAL_POSE_FRONT`, `ANIMAL_POSE_BACK`). What a
  settler carries is drawn behind the body of one walking away, and ahead
  of one seen from the side.
- Sleep: a settler asleep (always seen from the front) has closed eyes, a
  layer over the head (`eyes_closed_<skin>`: the skin of a head drawn
  without eyes, so that it matches, with a lid line; it leaves the brows, so
  it serves every expression), and three Z's rising from the head, growing,
  swaying and fading in and out (`render_sleep_zs()`, one sprite scaled by
  `sprites_draw_scaled()`, out of step by slot). The bar's portrait has the
  closed eyes too. Nothing is darkened any more.
- Gait: nobody has moving legs. Whoever walks hops at every step and rocks
  from side to side around its feet (`gait_at()`, `draw_walker()`), all
  layers as one; a fleeing animal steps faster. It depends on the game tick,
  like the hands.
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

The target layout of the interface, its behaviors and the plan to get there
are in `docs/UI.md`. Colors and sizes come from the theme (`game/theme.h`):
a color per role and every size multiplied by the UI scale (`--ui-scale`,
the font included); widgets never use literal colors or pixel sizes. The
zones of the screen are worked out in `game/layout.h`; windows stay above
the bottom bar (`Ui.reserved_bottom`).

The camera can follow a settler or an animal (`App.follow_kind`,
`App.follow_id`, `app_follow()`): `update_follow()` runs after the game's
update, so that the view and the sprite agree, and copies the being's
position and level into the camera; panning, dragging, another level, the
being's death or (for an animal) losing sight of it end it.

UI state that is not part of the simulation (camera, selection, selected
stockpile, gathering zone, workstation, tile or wild animal, active tool, drag
rectangle, debug overlay and menu toggles) lives in `App`. The level shown is
`Camera.level` (Page Up / Page Down, or Shift + wheel; it starts on the
level of the first settler; the HUD numbers it from `Map.surface_level`): the map, the entities and the overlays are
drawn for that level, and clicks and tools act on it (a selected structure
keeps its level in `App.selected_tile_z`). A click on the map selects a
colonist, else the item drawn under the cursor, else the first of what lies
on the tile (`ClickTarget`): the station, the structure, the roof, the
construction site, the items, the plant (a tree from any tile of its trunk or
foliage), the stockpile, the gathering zone, the grass; a click on what is
selected goes to the next one. Items are picked one by one
(`game_items_under_point()`: the drawn ones whose disc holds the point, the
topmost first, so repeated clicks go down a heap before the tile's things):
the item selected (`THING_ITEM`, `App.selected_item`, an `ItemPick`) is
followed when it moves (`game_pick_item()`: its tile, then around it) and
across the edge of the fog of war (`items_find_like()`: its copy in the
known world, or the real one of a copy), ringed on the map
(`render_item_selection()`) and has a window of its own (weight, condition,
rot, the other items of its tile). A box drawn on the map selects the
colonists in it, else the items in it (`game_items_in_rect()`: the drawn
ones whose center lies inside): one is selected as a click would, several
together (`THING_ITEM_BOX`, `App.selected_box_items`, each followed as above,
those gone dropped), all ringed, in a window that totals their weight and
lists them by kind, with Forbid and Carry elsewhere for all of them (an
order per item).
A double click (two clicks within `APP_DOUBLE_CLICK_MS` and
`APP_DOUBLE_CLICK_SLOP` pixels, the first having selected an item, the
tile's items or a plant: `select_same_on_screen()`) selects all of the same
kind on the screen instead of going to the next thing under the cursor: the
items of that type in the visible tiles on the item's level
(`game_items_of_type_in_rect()`, so other items do not use up the
capacity), as a box would; the plants of that kind whose tile is seen at that
level (`THING_PLANT_BOX`, `App.selected_plants`: the foot tiles, dropped when
the plant is gone), ringed, with Forbid and the orders the kind takes (fell,
harvest, uproot) for all of them. In the window of a tile's items, each line
of the list is a link that selects that item, and the "other items on this
tile" line of an item selects the tile's items (`ui_layout_link()`; the click
is applied after the window is drawn, `ThingClick`).
A body has a window of its own kind (`hud_corpse_rows()`,
`hud_corpse_tabs()`). A settler's is titled with the dead's name, with their
age, cause of death and how long ago, and has the tabs of a settler's window,
of which the Character, Knowledge and Journal show the record kept
(`Corpse.colonist`: read-only, the knowledge tooltips as of the death) and the
others only say the settler is dead; the tile readout names the owner. An
animal's carcass keeps its name as the title, tells the species, the sex, the
cause of death and how long ago, and has the animal's three tabs: Character
and Journal from the record (`Corpse.animal`), Health only saying it is dead.
Each thing on a tile (`TileThing`, `App.selected_thing`) has a window like a
workstation's (`game/tile_panel.h`): its name as the title and the action menu
right of it (`inspector_forbid_action()`, `inspector_order_action()` for
orders that toggle: lit while given, veiled while nobody knows how; a click
again takes the order back). Selection reads what the colony knows
(`game_view_map()`, `app_thing_at()`). What is
forbidden carries a "no entry" sign (`render_forbidden_markers()`); a
forbidden item among allowed ones on its tile, a small one of its own. Where the level shown is
open space, the levels below show through (`render_below_begin()`), however
deep. The level seen on each visible tile (`map_view_level()`) is worked out
once a frame; only the levels seen somewhere are drawn, deepest first, each
with its ground, structures, stations, items and sprites, but only on the
tiles where it is the level seen (`tile_seen()`; a tree also shows when only
its own trunk or foliage covers its foot). Each tile is thus drawn once
whatever the depth, on an offscreen image three times smaller then stretched
back with smoothing (a cheap blur). After each level a veil of haze makes up
the difference with the next level drawn, so that what lies d levels down
keeps `BELOW_FLOOR + (1 - BELOW_FLOOR) * BELOW_DECAY^d` of its color: a little
darker with each level, never black. (Drawing every level in full, as before,
cost a quarter of the frame rate at the surface level, the ground under it
drawn though hidden, and had to stop 6 levels down.) The ground of the level shown has a
dark edge over each drop, and slopes are lit towards the higher side.
Ramps (natural slopes and dug ones, not ladders) carry a triangle on the
level shown only (`render_ramp_markers()`, from `map_ramp_at()`): pointing up
on a ramp leading up from it, down on the open space over one leading down
from it; see-through, over the items, plants and settlers there.
Escape and a right click on the map
(released where it was pressed: otherwise it pans) step back one level
(`step_back()`), except that a right click with settlers selected and no tool,
menu or drag sends them there (`can_order_move()`, `game_order_move()`); with nothing left to close, Escape opens the game menu,
which pauses the game while it is open (as does the settings window opened
from it). The settings (`core/settings.h`) are read at start from
`settings.cfg` in `SDL_GetPrefPath("", "colony-sim")` and written again at
each change; `--ui-scale` and `--lang` override them for the session
without being saved. A new interface size builds a new `Theme`, opens the
font again at its size (`text_renderer_set_size()`) and places the windows
again (`ui_set_theme()`); a palette only swaps the theme's colors (the
temperature view takes its colors from the theme too). Keys without a character (Escape,
Space, Page Up/Down) are named through translations. Every keyboard control is a physical position (`APP_KEY_*`
scancodes in `game/app.h`): the controls are laid out the same on any
keyboard (WASD on QWERTY is ZQSD on AZERTY), and the help and tooltips show
the labels of the active layout (`app_key_name()`). The key at the physical position of A on AZERTY (Q on QWERTY,
`APP_KEY_BUILD_LIKE`) turns on the tool that builds another of what is
selected (`build_like_selection()`). X (`APP_KEY_CANCEL`) with no tool in use
acts on the selection (`app_cancel_selection()`): it deletes a stockpile or
gathering zone, cancels a construction site or the order given on a plant or
items, and toggles the order to take apart a structure or roof
(`tile_panel_cancel()`); built workstations are left alone (their delete
button removes them at once). Otherwise it turns on the cancel tool.
The zone tools have a mode (`App.zone_mode`): add, remove, and extend or
shrink only the selected zone (the two buttons of a zone's action menu,
`inspector_zone_edit_actions()`; `game_extend_stockpile_rect()`,
`game_shrink_gather_zone_rect()`...). The tool turns off when that zone goes.
The selected zone is drawn brighter, with a thick border.
The priority button of a workstation or site (`inspector_priority_action()`)
shows its number; a left click raises the priority (a smaller number), a
right click lowers it (`ui_icon_button_click()` tells the mouse button).
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

The inspector (`game/inspector.h`) is made of docked windows
(`ui_layout_docked()`): their bottom left corner is fixed (the box on the
bottom bar, the open tab's panel on the tab row above the box), they grow
with their content up to the colonist bar and scroll beyond, and they cannot
be dragged. Each kind of selection draws its summary, its tabs and its
actions through the inspector's functions; Escape closes the open tab
first, then the selection. Selecting something on the map closes the build
menu and the work grid, which hide the inspector while they are open.

The work grid and the colony log, main windows opened from the bottom bar
(one at a time, never with the build menu), are docked the same
way at the bottom left corner, above the bottom bar; its hint and legends
wrap to the width of the grid rather than widening it.

Other windows (controls help, debug, game menu) keep their position, stacking
order and scroll in `Ui` from one frame to the next. They are sized to their
content: `ui_layout_window()` runs the rows once to measure them, then again
to draw them (a NULL title gives a window without a title bar, its × at the top
right: the tab panels of the inspector) (rows of text, and `ui_layout_meter()`, `ui_layout_bar_row()`,
`ui_layout_pair()` and `ui_layout_triple()` for a bar under its label, a table row with a bar or
with two or three columns of text, colored with
`theme_scale_color()` / `theme_sign_color()`: red to green, orange to blue in
the colorblind palette). A window taller than the screen allows scrolls, with its
content clipped. A click brings a window to the front, and a window opened
again comes to the front; the HUD draws the open windows back to front
(`hud_open_windows()`), then the build menu and the bottom bar on top, then an open dropdown list,
then the tooltip. Widgets act on the game directly through core functions
(`stockpile_set_priority()`...). It never influences the simulation,
so the same seed gives the same game whatever the player looks at.

## Assets

`assets/` is copied next to the executable at each build (`bin/assets/`, the `colony_assets` target: only changed files, even when nothing is compiled).
At startup the game looks for `<executable dir>/assets/`, then falls back to
the source tree (`COLONY_SOURCE_ASSETS_DIR`, development builds).

## Build

- CMake ≥ 3.26, presets in `CMakePresets.json`: Ninja, 6 jobs (4 for the
  sanitized game build, which needs much memory), tests run 6 at a time.
- ccache is used when installed (`find_program()` in `CMakeLists.txt`).
- Unit tests: one executable per file of `tests/`. The test functions of the
  slowest files (`COLONY_SPLIT_TESTS`) are CTest entries of their own
  (`test_game.test_plants_grow_over_time`), run with `COLONY_TEST_ONLY`, so
  that a full `ctest` spreads over the cores instead of waiting for one file.
- SDL3 / SDL3_ttf: system packages if found, else `FetchContent` from the
  official Git repositories (pinned release tags, static libraries).
- Our targets get strict warnings via `colony_configure_target()`
  (`cmake/ProjectOptions.cmake`); third-party code does not.
