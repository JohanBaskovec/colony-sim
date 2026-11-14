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

- [ ] Designations: chop trees, mine rock, harvest plants
- [ ] Job system with reservations
- [ ] Items on the ground (wood, stone, food), stacking
- [ ] Food reservations (two colonists no longer walk to the same bush)
- [ ] Hauling and stockpile zones with filters
- [ ] Work priorities grid UI

## M3: Building

- [ ] Blueprints, construction jobs, materials
- [ ] Walls, doors, floors; furniture (bed, table, storage)
- [ ] Room detection, roofs
- [ ] UI framework: buttons, panels, tooltips (all translatable, layout adapts to text length)

## M4: Survival

- [ ] Seasons affect temperature and plant growth
- [ ] Temperature simulation (outdoor, rooms)
- [ ] Farming zones, sowing and harvesting
- [ ] Cooking, meals, spoilage
- [ ] Mood system with modifiers, mental breaks

## M5: Events and danger

- [ ] Event director
- [ ] Wildlife, hunting, animal attacks
- [ ] Health: injuries, treatment, disease; consequences of starvation and exhaustion
- [ ] Combat basics, raids

## M6: Persistence and polish

- [ ] Save/load (versioned), autosave
- [ ] Settings menu (language, resolution, volume, key bindings) persisted in the user preferences folder
- [ ] Main menu, new game options (seed, map size, difficulty)
- [ ] Sound and music
- [ ] Sprites/texture atlas instead of colored tiles
- [ ] Data-driven definitions (terrain, plants, items) + mod loading

## Technical backlog

- [ ] Remember the chosen language between sessions
- [ ] HarfBuzz + font fallback when a translation needs non-Latin scripts (would also allow re-enabling kerning, disabled because SDL_ttf mispositions kerned glyphs without HarfBuzz)
- [ ] More plural rules (Slavic languages, Arabic) when needed
- [ ] Batch tile rendering (texture atlas / geometry) for large maps at low zoom
- [x] Pathfinding benchmark (`bench/bench_pathfinding`)
- [ ] Connectivity regions: reject unreachable goals instantly (today they flood the whole area)
- [ ] Simulation benchmark (many colonists, long runs)
- [ ] Windows and macOS CI builds
- [ ] Choose a license
