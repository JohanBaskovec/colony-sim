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
- [ ] Hauling and stockpile zones with filters
- [ ] Work priorities grid UI (priorities exist in the simulation, default 3 for everything)

## M3: Building

- [ ] Blueprints, construction jobs, materials
- [ ] Walls, doors, floors; furniture (bed, storage) in stone age materials
- [ ] Room detection, roofs
- [ ] UI framework: buttons, panels, tooltips (all translatable, layout adapts to text length)

## M4: Survival

- [ ] Fire: campfires, light and heat, fuel
- [ ] Temperature simulation per tile: diffusion, wall insulation, doors, caves
- [ ] Seasons affect temperature and plant growth; weather
- [ ] Bladder and bowel needs, hygiene, latrines
- [ ] Containers (buckets) and roles: water carrier filling buckets near dwellings
- [ ] Gathering, cooking on fire, spoilage
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
- [ ] Skills and traits
- [ ] Era progression system; bronze age, then the following eras
- [ ] Pregnancy, children, aging
- [ ] Religion, spirituality levels, "inner peace" ending; other endings

## Technical backlog

- [ ] Remember the chosen language between sessions
- [ ] HarfBuzz + font fallback when a translation needs non-Latin scripts (would also allow re-enabling kerning, disabled because SDL_ttf mispositions kerned glyphs without HarfBuzz)
- [ ] More plural rules (Slavic languages, Arabic) when needed
- [ ] Batch tile rendering (texture atlas / geometry) for large maps at low zoom
- [x] Pathfinding benchmark (`bench/bench_pathfinding`)
- [ ] Connectivity regions: reject unreachable goals instantly (today they flood the whole area; failed food/water searches are only made rare by an exponential retry backoff)
- [ ] World generation: guarantee drinkable water near the start (small maps can have none; the spawn point only prefers water)
- [ ] Simulation benchmark (many colonists, long runs)
- [ ] Windows and macOS CI builds
- [ ] Choose a license
