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
- [ ] Skills also change work speed
- [ ] Haul more than one stack per trip; haul towards construction sites (M3)
- [ ] Carrying capacity from strength/health; containers (baskets, sledges) and worn equipment adding bulk and weight

## M3: Building

- [ ] Blueprints, construction jobs, materials; the quality of built things comes from the builder's skill and the quality of the materials used
- [ ] Walls, doors, floors; furniture (bed, storage) in stone age materials
- [ ] Room detection, roofs
- [ ] UI framework: buttons, panels, tooltips (all translatable, layout adapts to text length)

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
- [ ] Pixel art sprites (64 × 64 tiles) and texture atlas instead of colored tiles, with mipmaps for zoomed-out views
- [ ] Data-driven definitions (terrain, plants, items) + mod loading

## Later

- [ ] Social life: relationships, sexual needs (abstract representation)
- [ ] Traits; more skills as new kinds of work appear (basic skills are in M2)
- [ ] Era progression system; bronze age, then the following eras
- [ ] Pregnancy, children, aging
- [ ] Religion, spirituality levels, "inner peace" ending; other endings

## Technical backlog

- [ ] Remember the chosen language between sessions
- [ ] HarfBuzz + font fallback when a translation needs non-Latin scripts (would also allow re-enabling kerning, disabled because SDL_ttf mispositions kerned glyphs without HarfBuzz)
- [ ] More plural rules (Slavic languages, Arabic) when needed
- [x] Batch tile rendering (geometry) for large maps at low zoom; a texture atlas comes with real art
- [x] Pathfinding benchmark (`bench/bench_pathfinding`)
- [x] Connectivity regions: no search for unreachable goals, water or orders (`core/region.h`)
- [ ] Failed food searches still explore everything within their cost limit (1 to 4 ms with 64 hungry colonists and no food nearby): spatial index of food sources, or a faster priority queue
- [ ] World generation: guarantee drinkable water near the start (small maps can have none; the spawn point only prefers water)
- [x] Simulation benchmark (`bench/bench_simulation`: many colonists, long runs, state hash)
- [ ] Windows and macOS CI builds
- [ ] Choose a license
