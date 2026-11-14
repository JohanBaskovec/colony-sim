# Colony Sim

*Working title.* A top-down colony simulation game written in C17 with
[SDL3](https://libsdl.org): guide a small group of settlers from the stone age
onwards, manage their needs and moods, gather resources, build a base and
survive whatever the world throws at them.

> **Status: pre-alpha (milestone M2, work, in progress).** A procedurally
> generated map, a day/night cycle, and a small group of colonists who eat
> berries, drink at the shore, sleep, and chop trees or mine rock where the
> player orders it, leaving wood and stone on the ground. No hauling or
> construction yet: see the [roadmap](docs/ROADMAP.md).

## Features so far

- Deterministic procedural map generation (seeded): water, sand, soil, rock walls, grass, bushes, trees
- Fixed-rate simulation (60 ticks/s at normal speed), pause and ×1/×3/×6 speeds
- In-game calendar (days, seasons, years) and day/night lighting
- Plant growth that depends on terrain fertility
- Colonists with needs (food, water, rest) who decide on their own: eat berries, drink, sleep, wander
- Items on the ground (wood, stone) in stacks
- Work orders (designations): chop trees and mine rock; idle colonists pick jobs by work priority then distance, and reserve their job or food so that nobody else takes it
- A* pathfinding with terrain movement costs
- Selection (click, Shift+click, box) and a colonist inspector
- Pan/zoom camera, tile inspector
- **Multi-language**: English and French, switchable live; the language is picked from the system locale by default
- Simulation core is plain C with no SDL dependency, covered by unit tests

## Building

### Requirements

- A C17 compiler (GCC, Clang or MSVC)
- CMake ≥ 3.25
- Git (dependencies are fetched automatically)
- Linux: FreeType and the usual SDL3 build dependencies (X11/Wayland, ALSA/PulseAudio, EGL/GL headers).
  On Debian/Ubuntu:

  ```sh
  sudo apt install build-essential cmake git libfreetype-dev libx11-dev libxext-dev \
      libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libwayland-dev \
      libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev libdecor-0-dev
  ```

SDL3 and SDL3_ttf are used from the system when CMake can find them, otherwise
they are downloaded and built as static libraries during the first configure
(this takes a few minutes, once per build directory).

### With presets (recommended)

```sh
cmake --preset debug           # configure (first run downloads SDL3)
cmake --build --preset debug   # build
ctest --preset debug           # run the tests
./build/debug/bin/colony_sim   # play
```

| Preset     | Purpose                                                              |
|------------|----------------------------------------------------------------------|
| `debug`    | Game + tests, debug symbols                                          |
| `release`  | Optimized build                                                      |
| `asan`     | Debug + AddressSanitizer/UBSan                                       |
| `headless` | Simulation core + tests only: no SDL download, sanitizers, `-Werror` |

CLion and VS Code (CMake Tools) pick up the presets automatically. Editors
using clangd (Zed, VS Code with the clangd extension, Neovim...) read the
compilation database of the `debug` preset (see `.clangd`): run
`cmake --preset debug` once, then restart the language server.

### Command line

```
colony_sim [--lang <code>] [--seed <n>] [--size <n>] [--help]
```

- `--lang fr`: interface language (default: system locale, then English)
- `--seed 42`: world seed (default: random); the same seed always gives the same map
- `--size 256`: map width and height in tiles (16 to 1024, default 200)

## Controls

| Action                | Input                                                        |
|-----------------------|--------------------------------------------------------------|
| Move the camera       | W A S D (physical keys, Z Q S D on AZERTY) or arrows; Shift = faster |
| Drag the camera       | Middle or right mouse button                                 |
| Select colonists      | Left click (Shift: add/remove) or left drag for a box        |
| Chop / mine / cancel orders | C / M / X, then left click or drag over the tiles; Esc to go back |
| Zoom                  | Mouse wheel                                                  |
| Pause / resume        | Space                                                        |
| Speed ×1 / ×3 / ×6    | 1 / 2 / 3                                                    |
| Next language         | L                                                            |
| Debug overlay         | F3                                                           |
| Quit                  | Esc (when no tool is active)                                 |

## Translations

All player-facing text lives in `assets/lang/<code>.lang` files (UTF-8,
`key = value`). Adding a language is a matter of copying `en.lang`, translating
the values and running the tests, which check that no key or placeholder is
missing. See the [translator guide](docs/I18N.md).

## Project layout

```
assets/        Game data shipped next to the executable (fonts, translations)
cmake/         CMake helpers (dependencies, warnings, translation key check)
docs/          Design and technical documentation
src/core/      Simulation core: pure C17, no SDL, deterministic, unit-tested
src/game/      SDL3 frontend: window, input, rendering, HUD, localization loading
tests/         Unit tests (one executable per module) and translation checker
bench/         Performance benchmarks (not run by the tests)
```

## Documentation

- [Game design](docs/GAME_DESIGN.md): vision, pillars and planned systems
- [Architecture](docs/ARCHITECTURE.md): modules, game loop, determinism, rendering
- [Roadmap](docs/ROADMAP.md): milestones and progress
- [Internationalization](docs/I18N.md): file format, plurals, adding a language
- [Contributing](CONTRIBUTING.md): code style, tests, commits

## License

The project license is not chosen yet. The bundled DejaVu Sans font is
distributed under its own license (`assets/fonts/LICENSE-DejaVu.txt`).
