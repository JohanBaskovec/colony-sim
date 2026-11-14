# Roadmap

Milestones are ordered; each one ends with a playable build and passing tests.
Tick items as they land and add newly discovered tasks.

## M0: Foundations ✅

- [x] CMake project, presets, SDL3/SDL3_ttf fetched automatically
- [x] Core/frontend split (core has no SDL dependency)
- [x] Unit test framework + CTest integration, sanitizers, CI workflow
- [x] Logging, PCG32 RNG, value noise
- [x] i18n: catalogs, fallback, plurals, positional placeholders, number formatting
- [x] Translation checks (completeness, placeholders, keys used in code)
- [x] English and French translations, live language switching, system locale detection
- [x] Fixed-timestep game clock, speeds, pause, calendar, day/night
- [x] Procedural map: terrain, rock walls, grass/bushes/trees
- [x] Plant growth by fertility
- [x] Camera (pan, zoom at cursor), tile inspector, HUD, debug overlay

## M1: Colonists ✅

- [x] Entity pools with generational handles
- [x] Colonist entity: name, age, position; starting group spawned on a large reachable land area
- [x] A* pathfinding with movement costs, no corner cutting, nearest-match search (+ tests, + benchmark)
- [x] Movement along paths, interpolated rendering
- [x] Selection (click, Shift+click, box select), colonist inspector panel, path display
- [x] Needs: food and rest; simple decision making (eat berries, sleep in place / at night, wander)
- [x] Names: built-in multicultural list, not translated (proper nouns)

## M2: Work (next)

- [x] Thirst: drink from shallow water or a lake shore; eat and drink before going to sleep
- [x] Designations: chop trees, mine rock (tools, rectangle drag, cancel); idle colonists do the work, one worker per tile (reservations), interrupted work resumes
- [x] Job system: generic reservation table (jobs, food: two colonists no longer walk to the same bush), work types (mining, woodcutting) and per-colonist work priorities, jobs picked by priority then distance
- [x] Items on the ground (wood, stone), one stack per tile with stack limits; chopping and mining drop them, spreading to nearby reachable tiles
- [x] Harvest designation (gathering work): berries as a food item, the bush grows back; colonists eat food items from the ground as well as ripe bushes
- [x] Hauling and stockpile zones: zone tool (rectangle drag, extend, Shift to remove), per-zone priority from 1 (highest) to 10, allowed items by category (food, resources); colonists carry items to the best zone with room, and move stored items to better zones
- [x] Minimal immediate-mode UI (panels, buttons, check boxes) for the stockpile inspector
- [x] Item weight (kg, realistic) and bulk: stack limit per tile from bulk (1,000 berries, 50 logs, 50 stones); colonists carry a limited bulk and weight per trip, and walk slower when dragging more than their weight limit
- [x] Work priorities grid UI: colonists × work types, left click raises a priority, right click lowers it (1 to 4, empty = never); changes apply at once (a disabled work type is dropped, a newly enabled one is picked up without waiting)
- [x] Item quality and hit points: five quality grades (crude to superb) and hit points per item; only items of the same grade stack, a stack keeps the average hit points; raw stone is always superb and cannot be damaged
- [x] Skills (mining, woodcutting, gathering, levels 0 to 20, rolled at creation): the worker's skill sets the grade and condition of what it produces (novices bring back rough, damaged logs)
- [x] Stockpile filters on item condition: quality and hit point range sliders; items a narrowed filter no longer accepts are moved out
- [x] Skill learning: work on an order gives experience in its skill, each level takes longer (one in-game hour of work for level 1, twenty for level 20); progress shown in the colonist inspector, levels in the work priorities grid
- [x] Skills also change work speed: ×0.5 at level 0, ×1 at level 10, ×1.5 at level 20 (shown in the colonist inspector)
- [x] Haul more than one stack per trip: after a pickup, a hauler with room left collects nearby matching stacks bound for the same storage tile (one type and grade per load)
- [ ] Haul towards construction sites (M3); loads of mixed items once containers exist
- [x] Strength (1 to 20, rolled at creation, lower past 40): sets the weight a colonist carries without slowing down (22 to 60 kg, 40 kg at 10); shown in the colonist inspector
- [ ] Carrying capacity also from health (M6); containers (baskets, sledges) and worn equipment adding bulk and weight, once they can be made

## M3: Building

- [x] Design of the knowledge system and of the seven starting knowledges, their items and constructions (`docs/KNOWLEDGE.md`)
- [x] Knowledge definitions and per-settler levels (seven starting knowledges, 50 to 80 at the start), replacing skills: speed, quality and minimum levels from knowledge; learning by doing; start with no tools nor food, ripe berry bushes guaranteed near the arrival point; knowledge levels in the colonist inspector and the work grid
- [x] New materials and orders: gather stones (repeats until cancelled), gather branches, gather twigs (plant stocks that grow back), cut grass; fresh grass dries on the ground; mined stone renamed stone block; felling and mining locked until axes, picks and their knowledges exist
- [x] Workstations and bills: stone split into cobble and flint, flint patches (one near the start); workstations built from a blueprint by a builder who brings the materials (knapping spot), or ready at once when made of nothing (crafting spot); bills (do N times, until N lie in the colony, forever; allowed stone types) and crafting; carried tools (one per type, no equipment slot), fetched when a recipe needs one and kept; knapping and spear making recipes, hand axe failures, quality capped by materials; build tool, workstation inspector (recipes named after their product, with their ingredients and tools), flint and carried tools in the inspectors
- [x] Stone gathering zones instead of a repeating order: gather until the colony has a target of cobbles and of flint (or forever); stones no longer wanted are left; a background job, after construction and crafting at the same priority
- [x] One configurable gathering zone replaces the chop, harvest, branches, twigs, grass and stones orders: a check box per resource (wood, berries, branches, twigs, grass, flint, cobbles), each gathered N times, until N lie in the colony, or forever; wood and berries from fully grown plants only (option, on by default); plant stocks taken before fruit and felling on the same tile
- [ ] Dirt and knapping debris: needs a dirt layer, and injuries (M6) for the sharp debris
- [x] Bills: worker level filter (minimum/maximum level in the recipe's knowledge, −/+ by 5, Shift by 25; the inspector warns when nobody fits)
- [x] Bills: ingredient search radius (travel cost from the station, 10 to 250, −/+ by 10, Shift by 50), pausing a bill (a craft under way is finished); "until N in stock" counts stored items and those on their way to storage (lying on a workstation, carried by a hauler), refreshed each tick only while such a bill exists
- [ ] Workstations and blueprints on plants: clear the plant first (today a station may be placed on a bush or grass)
- [x] Zone editing: a rectangle over zones of the same kind merges them (the selected one, else the oldest, keeps its settings); removal tools in the build menu take tiles out of zones (Shift+drag still works)
- [x] Zones of different kinds never overlap: a gathering zone drawn over a stockpile (or the other way round) leaves its tiles out
- [ ] Gathering zones: forbid tiles under workstations, or decide how they mix; a zone priority; show the job under way when hovering a tile
- [ ] Tools (next): speed factors on orders and recipes, grade factor, wear and breaking (a cobble used as a hammer does not wear yet: cobbles are not damageable); swapping for a better tool; the detour in job search
- [ ] Starting settlers get 1 to 30 points in a few related knowledges (once non-starting knowledges exist)
- [x] Construction sites: blueprints of structures with several materials, builders fetching each material and working from a neighboring tile, cancelling gives the materials back, a *take apart* order (half the materials back)
- [x] First structures, with realistic material counts (dry grass only): grass bed (12 dry grass), grass and branch wall 2 m high (5 branches, 24 dry grass, drawn as the edges of a rectangle), branch screen as a door (5 branches, 5 twigs, ×1.5 movement cost)
- [x] Beds: settlers sleep in the nearest free bed; on the ground rest comes back at ×0.8 and they work 10% slower for 12 hours after
- [ ] Quality of built things from the builder's level and the materials; hit points, wear, refreshing bedding (`docs/KNOWLEDGE.md` §10.2)
- [ ] Beds owned by one settler; beds of 1 × 2 tiles with a rotation
- [ ] Haulers bring materials to construction sites (today the builder fetches them); a construction scenario in `bench_simulation`
- [ ] Lean-to (roof), cave floor clearing; workstations built through construction sites
- [ ] Room detection, roofs (lean-to, natural rock roof over caves)
- [ ] Knowledge spreading: conversation, apprenticeship, fading and loss; only discovered knowledges shown in the colonist inspector
- [x] UI framework: buttons, panels, tooltips, windows sized to their content (title bar to drag, × to close, stacking order, scroll bar and wheel when too tall), measured layouts that adapt to text length
- [x] Bottom left menus: Build (workstations, zones, orders as icon buttons with tooltips: name, description, cost, shortcut), Work, ? (controls help, F1); the tile inspector above them; Escape or a right click (not a pan) steps back (drag under way, tool, menu, front window) and never quits; A (AZERTY) / Q (QWERTY) builds another of what is selected (zone, workstation, structure; structures and sites can be selected)
- [ ] UI: horizontal scrolling (the work grid is wider than small screens), windows remembering their position between sessions, a shortcut shown for the workstation buttons (E cycles them)

## M4: Survival

- [ ] Fire: campfires, light and heat, fuel
- [ ] Temperature simulation per tile: diffusion, wall insulation, doors, caves
- [ ] Seasons affect temperature and plant growth; weather
- [ ] Bladder and bowel needs, hygiene, latrines
- [ ] Containers (buckets) and roles: water carrier filling buckets near dwellings
- [ ] Gathering, cooking on fire, spoilage (items lose hit points over time; outdoors faster than under a roof); food quality affects mood
- [ ] Mood system with modifiers, mental breaks

## M5: Vision and exploration

- [ ] Field of view per settler, walls block sight, peeking around corners
- [ ] Fog of war: unexplored map hidden, explored areas shown as last seen
- [ ] Wildlife, hunting

## M6: Danger

- [ ] Event director
- [ ] Health: injuries, treatment, disease; consequences of hunger, thirst and exhaustion
- [ ] Combat basics (line of sight, cover), raids, animal attacks

## M7: Persistence and polish

- [ ] Save/load (versioned), autosave
- [ ] Settings menu (language, resolution, volume, key bindings) persisted in the user preferences folder
- [ ] Main menu, new game options (seed, map size, difficulty)
- [ ] Sound and music
- [x] Sprites (128 × 128 pixels per tile) in a texture atlas with halved copies for zoomed-out views: trees and bushes (5 growth stages, bushes with and without berries), colonists in 4 layers (naked limbless body in 3 shapes, head with 4 expressions, beard, hair) in front view, a moving fist (with the tool of the job) while working
- [ ] Ground textures (soil, sand, gravel, stone, water, rock walls) with transitions between terrains
- [ ] Colonist sprites seen from the back and the sides, walking and sleeping poses
- [ ] Sprites for items, workstations and structures
- [ ] Clothes as sprite layers over the body (hide tunic, fur wrap...), once clothing exists
- [ ] Gender (or body types) for colonists, to tie beards and names together
- [ ] Data-driven definitions (terrain, plants, items) + mod loading

## Later

- [ ] Social life: relationships, sexual needs (abstract representation)
- [ ] Traits; related practice and the knowledges after the starting ones; classes with classrooms and schedules
- [ ] Content of the later historical periods (Mesolithic to Iron Age, then beyond)
- [ ] Earth building with settling down: clay deposits, wattle and daub on the branch frame of grass walls, then cob, mud bricks, rammed earth, fired bricks (`docs/KNOWLEDGE.md` §13)
- [ ] Pregnancy, children, aging
- [ ] Religion, spirituality levels, "inner peace" ending; other endings

## Technical backlog

- [ ] Remember the chosen language between sessions
- [x] Every keyboard control by physical position (scancodes): the same layout on QWERTY, AZERTY and others, labels shown from the active layout
- [ ] HarfBuzz + font fallback when a translation needs non-Latin scripts (would also allow re-enabling kerning, disabled because SDL_ttf mispositions kerned glyphs without HarfBuzz)
- [ ] More plural rules (Slavic languages, Arabic) when needed
- [x] Batch tile rendering (geometry) for large maps at low zoom; sprites come from a texture atlas
- [x] Pathfinding benchmark (`bench/bench_pathfinding`)
- [x] Connectivity regions: no search for unreachable goals, water or orders (`core/region.h`)
- [ ] Failed food searches still explore everything within their cost limit (1 to 4 ms with 64 hungry colonists and no food nearby): spatial index of food sources, or a faster priority queue
- [ ] World generation: guarantee drinkable water near the start (small maps can have none; the spawn point only prefers water)
- [x] Simulation benchmark (`bench/bench_simulation`: many colonists, long runs, state hash)
- [ ] Windows and macOS CI builds
- [ ] Choose a license
