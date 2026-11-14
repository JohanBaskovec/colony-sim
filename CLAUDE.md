# CLAUDE.md

Guidance for Claude Code (and other AI assistants) working in this repository.

## Project

A top-down colony simulation game (genre: colony sim, think "settlers with
needs, jobs, construction and random events") written in **C17** with **SDL3**,
**SDL3_ttf** and **CMake**. The game is original: never reproduce names, art,
text or content from existing games. Pre-alpha: see `docs/ROADMAP.md` for the
current milestone.

## Language rules (important)

- **All code is written in English: identifiers, comments, log messages,
  commit messages, documentation files.** No French in the source tree, except
  inside translation files (`assets/lang/fr.lang`).
- The user (project owner) speaks French: talk to them in French in chat.
- **The game is multi-language.** Never hardcode player-visible text in C code:
  every string shown to the player goes through `tr()`, `tr_plural()`,
  `TR_FORMAT()`, `tr_integer()` / `tr_decimal()` (see `src/core/i18n.h`).
  - New key → add it to `assets/lang/en.lang` **and** every other language file
    (French included: translate it properly, with French typography: a
    non-breaking space U+00A0 before `:` and `%`, and as thousands separator).
  - Keys stored in static tables must be wrapped in `N_("...")`.
  - Use positional placeholders `{0}`, `{1}`: never build sentences by
    concatenating fragments (word order differs between languages).
  - Numbers shown to the player use `tr_integer()` / `tr_decimal()`.
  - Developer-facing text (logs, CLI usage, fatal startup errors) stays in English and untranslated.
  - Keyboard: physical-position keys use scancodes (WASD = ZQSD on AZERTY);
    mnemonic keys use keycodes. Display key names via SDL, not hardcoded.

## Commands

```sh
cmake --preset headless && cmake --build --preset headless && ctest --preset headless  # core + tests, fast, no SDL
cmake --preset debug && cmake --build --preset debug && ctest --preset debug           # everything
./build/debug/bin/colony_sim --lang fr --seed 42                                        # run the game
ctest --preset debug -R i18n          # only one test (regex on the test name)
ctest --preset debug -L i18n          # translation checks only (label)
./build/headless/bench/bench_pathfinding 200 2000   # benchmark (use an optimized build for real numbers)
```

The first `debug`/`release`/`asan` configure downloads and builds SDL3 (minutes).
Prefer the `headless` preset while working on the simulation core.

## Architecture (summary, details in docs/ARCHITECTURE.md)

- `src/core/` → static library `colony_core`. **Pure C, no SDL include, no file
  I/O, no wall-clock time, no `rand()`.** Deterministic: same seed + same inputs
  = same result (checked with `map_hash()`). All randomness goes through an
  explicitly seeded `Rng` (`core/rng.h`).
- `src/game/` → executable `colony_sim`. SDL3 frontend: window, input, rendering,
  HUD, asset/translation loading (`SDL_LoadFile`), then hands data to the core.
- Fixed timestep: `game_update()` converts real time into whole ticks
  (60 ticks/s at ×1); simulation logic lives in `game_tick()` and must only
  depend on the tick count, never on frame time.
- Static data tables (`TerrainDef`, `PlantDef`...) have a stable `id` (saves,
  never translated) and a `name_key` (translation key).
- Entities (colonists...) live in fixed arrays managed by a `Pool` and are
  referenced by generational `EntityId` handles, never by pointers kept across
  ticks. UI state (selection, camera) stays in the frontend (`App`).

## Conventions

- Files: `snake_case.c/.h`, module prefix for functions (`map_init`, `camera_zoom_at`).
- Types: `PascalCase` typedefs (`Map`, `GameClock`). Enums: `UPPER_SNAKE` with a type prefix (`TERRAIN_SOIL`).
- Macros/constants: `UPPER_SNAKE`. Static (file-local) functions for everything not in a header.
- Include guards: `COLONY_<DIR>_<FILE>_H`. Includes: `"core/xxx.h"` relative to `src/`.
- 4-space indentation, 100 columns, Linux brace style (`.clang-format`).
- Explicit ownership: `xxx_init`/`xxx_free` (caller-owned struct) or
  `xxx_create`/`xxx_destroy` (heap-allocated opaque type). Functions that can
  fail return `bool`. Check every allocation.
- Warnings are errors in CI (`-Wall -Wextra -Wpedantic -Wshadow -Wmissing-prototypes ...`).
  Compare enums as `(unsigned)value < COUNT` to avoid `-Wtype-limits`.
- Every header starts with a short comment explaining the module's purpose.

## Tests (mandatory)

- Every change to `src/core/` comes with unit tests in `tests/test_<module>.c`
  (framework: `tests/test_framework.h`, `CHECK*` / `REQUIRE` / `RUN_TEST`).
  New test file → add it to `COLONY_UNIT_TESTS` in `tests/CMakeLists.txt`.
- Write tests alongside the code, not afterwards; when fixing a bug, first add a test that reproduces it.
- Translation tests run automatically: each `.lang` file is checked against
  `en.lang` (missing/extra keys, placeholder mismatch) and every literal key
  used in `src/` must exist in `en.lang`.
- Before declaring a task done: build with `-Werror` (headless preset) and make `ctest` pass.
- Frontend code (`src/game/`) is not unit-tested: keep logic out of it and move
  anything testable into the core.

## Workflow

- Keep `docs/ROADMAP.md` up to date (tick items, add discovered tasks).
- Update `docs/ARCHITECTURE.md` when adding a module or changing a design decision.
- Commit only when asked. Commit messages in English, imperative mood
  (`Add pathfinding module`), small focused commits.
