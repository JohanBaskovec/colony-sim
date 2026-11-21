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
  - Keys stored in static tables must be wrapped in `N_("...")` (`NP_("...")` for
    the base of a plural passed to `tr_plural()`).
  - Use positional placeholders `{0}`, `{1}`: never build sentences by
    concatenating fragments (word order differs between languages).
  - Numbers shown to the player use `tr_integer()` / `tr_decimal()`.
  - Developer-facing text (logs, CLI usage, fatal startup errors) stays in English and untranslated.
  - Keyboard: every control is a physical position (scancode, `APP_KEY_*` in
    `src/game/app.h`), so controls are laid out the same on any keyboard
    (WASD on QWERTY = ZQSD on AZERTY). Display key names with
    `app_key_name()` (the label on the active layout), never hardcoded.

## Commands

```sh
cmake --preset headless && cmake --build --preset headless && ctest --preset headless  # core + tests, fast, no SDL
cmake --preset debug && cmake --build --preset debug && ctest --preset debug           # everything
./build/debug/bin/colony_sim --lang fr                                                  # the title screen (Thawmoor): New game, Settings, Quit
./build/debug/bin/colony_sim --lang fr --seed 42                                        # straight into a game (any game option skips the title screen)
./build/debug/bin/colony_sim --seed 42 --colonists 8 --scenario 3 --debug instant_build,learn_all  # test setup
./build/debug/bin/colony_sim --seed 42 --debug see_everything                          # no fog of war (docs/VISION.md)
./build/debug/bin/colony_sim --seed 42 --debug see_everything --animals 300            # three times the wild animals (0: none; 256 at most live at once)
./build/debug/bin/colony_sim --seed 7 --river 12 --debug see_everything                 # a river 12 tiles wide (0: none, the default on the command line; 6 in a New game)
./build/debug/bin/colony_sim --ui-scale 150         # larger interface (75 to 200 percent)
ctest --preset debug -R i18n          # only one test (regex on the test name)
ctest --preset headless -R test_game.test_plant_conditions  # one function of a split file (COLONY_SPLIT_TESTS)
COLONY_TEST_ONLY=test_plant_conditions ./build/headless/tests/test_game  # the same, by hand
ctest --preset debug -L i18n          # translation checks only (label)
./build/headless/bench/bench_pathfinding 200 2000   # benchmark (use an optimized build for real numbers)
./build/release/bench/bench_simulation 64 10 1      # 64 colonists, 10 days, scenario 1: tick times (processor time: steady on a busy machine), percentiles, slowest ticks, state hash (3: construction, 5: cooking), heaps, rot, bills, items check
BENCH_CHECK_ITEMS=1 ./build/headless-release/bench/bench_simulation 12 4 5  # also checks the items every in-game hour (game_check_items()), stops at the first problem
cmake --preset headless-release && cmake --build --preset headless-release  # optimized core + benchmarks, no SDL (for simulations)
./build/headless-release/bench/bench_social 8 10 4  # social life of 8 settlers for 10 days, scenario 4 (two feuding clans): chats, arguments, brawls, apologies, company per day, opinions (setup after the seed: discord, feud, abrasive, kind, loners, sociable)
./build/headless-release/bench/bench_social suite 5 42  # a line per social case (ordinary, idle, feuds, discord, loners, a crowd...): compare before and after a change
./build/debug/bin/colony_sim --seed 42 --scenario 4 --debug character_editor  # feuding clans; the Social tab gets the social debug tools
./build/release/bench/bench_simulation 12 4 1 200 42 - fog-revealed  # the fog's own cost (`fog`: settlers also search in vain for what they have not found)
./build/release/bench/bench_simulation 12 4 1 200 42 - - wildlife   # with wild animals (none otherwise): their cost, and how they fare
./build/release/bench/bench_vision 200 100 1      # field of view: cost of a whole cast and of a near cast, tiles seen, ray steps
./build/headless-release/bench/bench_water 256 5000  # water flow: a dam opened, a pond spreading over a plain, a channel (cost a step, blocks awake, water kept)
./build/release/bin/colony_sim --seed 42 --bench-render 300  # render benchmark: frame and drawing times of fixed views, no vsync, then quits
./build/release/bin/colony_sim --no-vsync           # uncapped frame rate; F3 also shows the drawing time per frame
cmake --preset profile && cmake --build --preset profile  # optimized + debug symbols + frame pointers, for perf
perf record -g ./build/profile/bench/bench_simulation 64 4 3 && perf report  # where the time goes
BENCH_TIMESTAMPS=1 perf record -g -k CLOCK_MONOTONIC ./build/profile/bench/bench_simulation 64 10 3  # then perf report --time <printed range>: one slow tick
python3 tools/generate_sprites.py --preview /tmp/sheet.png  # redraw assets/sprites/*.png (Pillow + numpy)
python3 tools/sprite_workbench.py    # window to tune the sprite and ground scripts of tools/assets/sprites/ (knobs, preview, save PNG)
python3 tools/preview_attacks.py --lang fr --output /tmp/attacks.gif  # GIF of the attack animations and marks (mirrors core/strike_anim.c, game/strike_marks.c)
python3 tools/preview_blood.py --output /tmp/blood.gif  # GIF of blood trails and pools (mirrors core/blood.c, render_blood())
clang-format -i src/core/foo.c                 # format what you changed (config: .clang-format)
clang-format --dry-run --Werror $(git ls-files '*.c' '*.h')  # check the whole tree: must print nothing
```

The `asan` preset builds the whole game (frontend included) with `-Werror` and
AddressSanitizer + UBSan: run it (`cmake --preset asan && cmake --build --preset asan
&& ctest --preset asan`) after touching `src/game/`.
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
- Terrain and rock are changed through `map_set_terrain()` / `map_set_rock()`
  (never by writing the layers directly once a `Game` runs): caches such as
  connectivity regions rely on `Map.topology_revision`.
- Performance work is measured with `bench_simulation` (simulation) and
  `--bench-render` (drawing), and profiled with `perf` on the `profile` preset;
  compare a baseline and the change run one after the other on the same
  machine. A change that should not alter behavior must leave the state hash
  of `bench_simulation` unchanged.
- **Saves** (docs/SAVE.md): every module saves itself (`xxx_serialize()`,
  one function for writing and reading, `core/serialize.h`). A field added to
  a struct that is saved must be serialized there (or left out with a reason
  in docs/SAVE.md §3), with `SAVE_VERSION` raised: `test_save`'s size test
  fails until then, and its tests check that a game loaded plays on byte for
  byte as it would have.
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
  New test file → add it to `COLONY_UNIT_TESTS` in `tests/CMakeLists.txt`
  (and to `COLONY_SPLIT_TESTS` if it takes seconds: each `RUN_TEST(name);`
  line then becomes a CTest entry of its own, run in parallel).
- Write tests alongside the code, not afterwards; when fixing a bug, first add a test that reproduces it.
- Translation tests run automatically: each `.lang` file is checked against
  `en.lang` (missing/extra keys, placeholder mismatch) and every literal key
  used in `src/` must exist in `en.lang`.
- Before declaring a task done: build with `-Werror` (headless preset) and make `ctest` pass.
- Before declaring a task done, also run `clang-format -i` on every `.c`/`.h` file you changed: the whole tree is kept clang-format clean.
- A pre-commit hook (`tools/git-hooks/pre-commit`) refuses staged C files that are not clang-format clean. Enable it once per clone: `git config core.hooksPath tools/git-hooks` (already done in this one).
- Frontend code (`src/game/`) is not unit-tested: keep logic out of it and move
  anything testable into the core.
- Visual checks of the running game: use a virtual display, never the user's
  desktop (`Xvfb :99 -screen 0 1280x800x24 &`, then `SDL_VIDEO_DRIVER=x11 DISPLAY=:99`
  for the game, so that SDL never falls back to the Wayland desktop, `xdotool`
  and `import -window root shot.png`). Also set `XDG_DATA_HOME` to a scratch
  folder: changing a setting in the game saves it, and it must not overwrite
  the user's own `settings.cfg`.

## Workflow

- Keep `docs/ROADMAP.md` up to date (tick items, add discovered tasks).
- Update `docs/ARCHITECTURE.md` when adding a module or changing a design decision.
- Commit only when asked. Commit messages in English, imperative mood
  (`Add pathfinding module`), small focused commits.
