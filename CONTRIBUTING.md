# Contributing

## Ground rules

1. **English everywhere in the code**: identifiers, comments, logs, commit messages.
2. **No hardcoded player-visible text**: use the i18n API (`tr()` and friends) and
   add the keys to every language file. See [docs/I18N.md](docs/I18N.md).
3. **Tests come with the code.** Any new or changed behavior in `src/core/` has
   unit tests; any bug fix starts with a failing test.
4. **The simulation core stays pure and deterministic**: no SDL, no I/O, no
   wall-clock time, no `rand()` in `src/core/`.

## Code style

- C17, compiled with `-Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
  -Wmissing-prototypes -Wvla -Wformat=2 -Wundef`. CI treats warnings as errors.
- Formatting follows `.clang-format` (4 spaces, 100 columns, Linux braces). Run
  `clang-format -i` on touched files when available.
- Naming:

  | Element            | Style                        | Example                     |
  |--------------------|------------------------------|-----------------------------|
  | Files              | `snake_case`                 | `game_clock.c`              |
  | Functions          | `module_action`              | `camera_zoom_at()`          |
  | Types              | `PascalCase`                 | `WorldGenParams`            |
  | Enum values        | `PREFIX_UPPER_SNAKE`         | `PLANT_TREE`                |
  | Macros, constants  | `UPPER_SNAKE`                | `TICKS_PER_DAY`             |
  | Translation keys   | `lower.dotted_snake`         | `hud.tile.terrain`          |

- One module = one `.h` (public API, documented) + one `.c`. Everything not in
  the header is `static`.
- Prefer plain data structs and free functions. Ownership is explicit:
  `init`/`free` for caller-owned structs, `create`/`destroy` for heap objects.
- Return `bool` for operations that can fail; log the reason with `LOG_ERROR`/`LOG_WARN`.
- Use fixed-size integer types (`uint8_t`, `int32_t`...) for data that is stored or hashed.

## Tests

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless              # all tests
ctest --preset headless -R worldgen  # one test
```

- Framework: `tests/test_framework.h` (`CHECK`, `CHECK_EQ_INT`, `CHECK_STR_EQ`,
  `CHECK_NEAR`, `REQUIRE`, `RUN_TEST`, `test_summary()`).
- One executable per module: `tests/test_<module>.c`, registered in `tests/CMakeLists.txt`.
- Test names describe behavior: `test_zoom_keeps_point_under_cursor`.
- Tests must be deterministic: fixed seeds, no timing dependencies.

## Commits

- Imperative mood, English, ≤ 72 characters for the subject: `Add A* pathfinding`.
- One logical change per commit; the build and tests pass at every commit.
