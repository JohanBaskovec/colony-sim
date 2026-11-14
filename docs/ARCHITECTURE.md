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
│  game ─► colonist ─► pathfinding ─► map                      │
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
| `colonist`   | Colonist data, needs, decision making, movement along paths                  |
| `pathfinding`| A* to a goal, Dijkstra to the nearest tile matching a predicate             |
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
- Expensive systems run at intervals rather than every tick (plants grow once
  per in-game hour). Future systems should do the same (e.g. needs every 60
  ticks, temperature every 250 ticks) and spread work across ticks.

## World representation

The map stores **dense layers**, one array per layer indexed by
`y * width + x`:

- `terrain[]`: `Terrain` (water, sand, soil...), each with a `TerrainDef`
  (movement cost, fertility, buildable);
- `rock[]`: natural rock walls (`RockType`, 0 = none), impassable;
- `plants[]`: at most one plant per tile (`Plant`: type + growth).

Dense layers are cache-friendly, trivially serializable and hashable
(`map_hash()`). Upcoming layers: floors, buildings/walls, zones, designations,
room IDs, temperature, fog of war.

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

1. updates needs (food always decreases; rest decreases, or increases while sleeping);
2. when standing on a tile center, decides: hungry → walk to the nearest
   edible plant (Dijkstra search); tired, or night and not fully rested →
   sleep in place; otherwise wander nearby after a random pause;
3. acts: walks one tick along its path (speed / tile cost), eats, or sleeps.

Decisions only happen on tile centers, so a colonist never jumps back when it
changes its mind. This simple state machine will be replaced by the job system
in M2 (designations, reservations, work priorities).

## Pathfinding

- 8-directional grid, cost of entering a tile = `map_move_cost()` (× √2
  diagonally), no diagonal move past an impassable corner.
- A* with the octile heuristic scaled by `PATH_MIN_TILE_COST` (admissible as
  long as no passable tile is cheaper: checked by a test).
- `pathfinder_find_nearest()` runs Dijkstra until a predicate matches (food,
  free tile...), with a cost limit to bound the search.
- A `Pathfinder` owns per-tile scratch arrays reused across searches (stamps
  instead of clearing), so searches do not allocate.
- Benchmark: `bench/bench_pathfinding [size] [searches] [seed]`; about 0.8 ms
  per cross-map search on a 200 × 200 map in Release. Unreachable goals
  currently flood their whole area: connectivity regions are on the backlog.

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
- Placeholder art: tiles are colored rectangles with a per-tile brightness
  variation. Replacing them with a texture atlas is a roadmap item.
- Text: SDL3_ttf renderer text engine, with a cache keyed by string content;
  entries unused for 120 frames are evicted.

## Frontend state

UI state that is not part of the simulation (camera, selection, drag
rectangle, debug toggle) lives in `App`. It never influences the simulation,
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
