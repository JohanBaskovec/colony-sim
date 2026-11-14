# Roadmap

Milestones are ordered; each one ends with a playable build and passing tests.
Tick items as they land and add newly discovered tasks.

## M0: Foundations ✅ (current)

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

## M1: Colonists

- [ ] Entity pools with generational handles
- [ ] Colonist entity: name, position, basic stats; spawn a starting group on passable land
- [ ] A* pathfinding with movement costs (+ tests, + benchmark)
- [ ] Movement along paths, interpolated rendering
- [ ] Selection (click, box select), colonist inspector panel
- [ ] Needs: hunger, rest; simple decision making (eat, sleep, wander)
- [ ] Name generation (per-language name lists?) **(design decision needed)**

## M2: Work

- [ ] Designations: chop trees, mine rock, harvest plants
- [ ] Job system with reservations
- [ ] Items on the ground (wood, stone, food), stacking
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
- [ ] Health: injuries, treatment, disease
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
- [ ] HarfBuzz + font fallback when a translation needs non-Latin scripts
- [ ] More plural rules (Slavic languages, Arabic) when needed
- [ ] Batch tile rendering (texture atlas / geometry) for large maps at low zoom
- [ ] Headless benchmark executable for simulation performance
- [ ] Windows and macOS CI builds
- [ ] Choose a license
