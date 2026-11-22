# Saving and loading

A game is saved to a file and loaded back exactly: a game loaded and played
on gives, tick for tick, what the game would have given had it never been
saved. This document is the design; §6 and §7 are what was built, §8 the
steps.

## 1. Principles

- **Exact.** Everything the simulation reads on a later tick is saved, or
  rebuilt from what is saved into the very same values. A cache that is
  rebuilt differently (region numbers in another order, a stock counted at
  another tick) is a bug: the save would change the future.
- **The core does no file I/O** (CLAUDE.md): it writes the save into a block
  of memory and reads it back from one (`core/save.h`); the frontend reads and
  writes the file (`SDL_LoadFile`, `SDL_SaveFile`).
- **Each module saves itself**, next to its own code: `xxx_serialize()` in
  `core/xxx.c` knows its fields; `core/save.c` only puts the sections
  together, in order.
- **One function to write and to read.** A module's `xxx_serialize()` walks
  its fields once, through a `Serializer` that writes them or reads them
  (`ser_u32(s, &x->count)`). Writing and reading cannot drift apart.
- **Field by field, little-endian, no padding.** Fixed sizes on the file
  whatever the platform (`int` as 32 bits, `long` and `size_t` as 64, floats
  as their IEEE bits, enums as their value). The same state always gives the
  same bytes: two saves can be compared byte for byte.
- **No compatibility between versions yet** (pre-alpha). A save carries the
  format's version (`SAVE_VERSION`); another version is refused with a clear
  message. Migrations come with the first public release, by version number
  in the reading code.

## 2. The file

```
header   "THMR" magic, u32 SAVE_VERSION
INFO     what the save tells of itself (for the list of saves)
section  4-byte tag, u32 byte length, the module's bytes   (repeated)
footer   u64 FNV-1a hash of everything before it
```

- The sections come in a fixed order, each one checked: a missing or
  unknown section, a length not read to its end, a value out of range (a
  count past its array, an enum past its count, a map larger than allowed)
  refuses the save. Nothing read is trusted: a damaged or hand-edited file
  must never crash the loading (see §6 for what is checked).
- The first section after INFO (`GAME`) holds what the others need to be allocated: the
  map's size and levels, the seed, the game's flags.
- A set of item types (`ItemTypeMask`: a stockpile's filter, a bill's inputs, a
  hearth's fuels) is saved as its 64-bit words (`item_types_serialize()`), one word
  while there are fewer than 64 types: past 64, a save takes one more word, and
  `SAVE_VERSION` is raised with the new types.
- No compression for now: a 256 × 256 map of 8 levels is a few megabytes.
  Run-length encoding of the layers may come if files prove large.

## 3. What is saved

Saved: the map and all its layers, revisions and logs; every module of
`Game` with its state (designations, dirt, zones, items and their pool,
crowns, corpses, stockpiles, workstations and bills, construction sites,
beds, reservations, item moves, allowed areas (their tile counts worked out again
on load), the clock's tick, every random stream,
temperatures, soil, the water's active blocks and their flows, the land
beyond the map's edges, the pore water of the ground (`Map.pore`) and the
mud (`Map.mud`) with the
water table beyond the edges and what the ground gave to sleeping lakes
this hour (`GroundWater.pending`), the light's
sweeps, the vision and the known world while the fog is on, weather, the
colonists, animals, social bonds, occupancy, notices, debug flags); the
counters the simulation compares (revisions seen, stamps of failed
searches, `Game.stock`).

Not saved, rebuilt on load:

- scratch memory (the pathfinder's nodes, heap and memo, the temperature
  buffers), pointers between modules (`Map.crowns`, `Items.mirror`, the
  drop filter, the temperature surface);
- what a step works out afresh before reading it: whether each active water
  block is ready to sleep (`WaterBlock.ready`);
- statistics nothing reads back: the water moved in the ground and seeped
  out over the last hours (`GroundWater.moved`, `seeped`);
- what the frontend only shows: the last blows to play (`CombatLog`), the
  conversation counts for the curious, the clock's speed (a loaded game
  starts paused) and its pending fraction of a tick.

Caches rebuilt from the saved state (regions...) are rebuilt only where the
round-trip test (§5) shows they come back the same; otherwise they are saved.

## 4. Where it lives

- `core/serialize.h`: the `Serializer` (a growing buffer when writing, a
  bounded reader when reading, a sticky error), the primitives (`ser_u8` to
  `ser_u64`, `ser_i32`, `ser_f32`, `ser_f64`, `ser_bool`, arrays of each,
  `ser_bytes`), sections and the checks of a count or an enum read.
- `xxx_serialize(Serializer *s, Xxx *x)` in each module.
- `core/save.h`: `game_save()` (a game into a buffer) and `game_load()` (a
  buffer into a new game, which owns its memory as after `game_init()`), and
  the reasons a save is refused (`SaveError`) for the frontend to tell.
- `core/save_files.h`: the names of the files (`save-001.sav`...,
  `quicksave.sav`, `autosave.sav`, `autosave-previous.sav`).
- Frontend (`game/saves.h`, `game/saves_window.h`, docs/UI.md *Saves*):
  *Save* and *Load* in the game menu, *Load game* on the title screen, the
  files in `saves/` in the user's data folder (`SDL_GetPrefPath()`:
  `~/.local/share/colony-sim/saves/` on Linux), a quick save (F8, F9) and
  an autosave at each new day of the game (the previous one kept).

## 5. Tests

- **Round trip.** Save a game, load it, save it again: the same bytes.
- **The future unchanged.** Play a game N ticks, save it, play on M ticks;
  load the save and play M ticks: both saves at N + M are the same bytes
  (stronger than `game_hash()`, which leaves out what it does not hash). Run
  on games that use every system: settlers working and hauling, building,
  crafting with bills, a storm with water flowing, animals hunting, a
  brawl, the fog of war, fires burning.
- **Refused.** A truncated save, a byte changed, another version, a count
  past its array: refused with the right reason, nothing leaked (asan).
- **Sizes.** The size of every serialized struct is checked (on 64-bit
  Linux): a field added to one fails the test until its `_serialize()` is
  updated and `SAVE_VERSION` raised.

## 6. As built (S1)

- `core/serialize.h`, `core/save.h`; `xxx_serialize()` in every module
  (`map`, `item`, `crown`, `colonist`, `animal`, `water`, `tile_light`,
  `vision`, `known`...). Sections: GAME (the size of the map, the seed, the
  tick, the random streams, the game's own fields), MAP, DSGN, DIRT, GZON,
  STCK, WKST, CNST, BEDS, RSRV, MOVE, ITEM, CRWN, DEAD, TEMP, SOIL, WATR,
  LIGH, SKY, SETL (the settlers' pool, the settlers, the social bonds), ANIM,
  BEIN (occupancy, the blows waiting, blood, the colony log), VISN and KNOW
  (each with a flag: only while followed), AREA (the allowed areas: names and
  the per-tile layer; the tile counts are worked out again on load, and a bit
  of an area that does not exist is refused; since SAVE_VERSION 11). WKST
  holds the ingredients of each craft in progress, one stack per ingredient
  of the recipe (up to three, docs/CONTAINERS.md §9; since SAVE_VERSION 12).
- **Loading** reads GAME, allocates the map and reads it, allocates every
  other module without working anything out (`game_prepare_load()`; the
  light, temperatures and soil have an `_init_empty()`: their full init was
  most of a second on a large map), reads the rest, then sets the pointers
  between modules again (the mirror of the known items, `Map.litter`,
  `Map.crowns`, the drop filter) and rebuilds the regions (they come back the
  same: the tests show it). The clock is paused.
- **Saved although rebuilt by the game in time**: occupancy (each being
  claims its tile again on its turn), the temperatures' caches of the map;
  cheap, and exact at once.
- **Not saved**: the pathfinder (scratch), `CombatLog`, `ConversationCounts`,
  the clock's speed and pending fraction of a tick.
- **Checks**: what indexes a table is checked when read (terrains, rocks,
  structures, plants, item types, enums, counts, chain links, indices of
  sources and blocks); a damaged file is refused by its hash. A hand-edited
  file that passes the hash loads or is refused without harm (tested), but
  values that only matter later (an index in a settler's job) are not all
  checked: such a save may misbehave in play.
- **Measured** (`bench_simulation` prints it after its run): 256 × 256 × 30
  with 12 settlers after a day, 6.5 MB, saved in about 90 ms, loaded in
  about 155 ms; 200 × 200 after four days of building, 4.5 MB.
- **Tests** (`tests/test_save.c`, `tests/test_serialize.c`): the round trip
  and the future unchanged on a new game, gathering, building, feuds and
  brawls, cooking, wild animals, the fog of war, water flowing and water
  flowing out over the map's edge, a river flowing when saved; saves cut short, changed, of another version, and 300 edited ones
  with their hash made right; the size of 80 structs. Leaving a field out of
  a settler's save fails them (checked by hand).

## 7. As built (S2)

- **INFO**, right after the header: the tick, the settlers alive, the seed,
  the map's size, whether the colony is lost. `save_read_info()` reads it
  from the first `SAVE_INFO_BYTES` of a file: the saves window lists saves
  without loading them. `save_check()` is what `game_load()` checks first
  (magic, version, hash).
- **Writing**: `game_save()`, then `SDL_SaveFile()` to `name.sav.tmp`, then
  `SDL_RenamePath()` over `name.sav`: a save cut short (the game killed, a
  disk full) never replaces a whole one. The autosave first renames the last
  one to `autosave-previous.sav`.
- **Loading** waits for the start of the next frame (never while windows are
  drawn). The whole file is read and checked (`save_check()`); a save refused
  there leaves the game in play, with a message (damaged, another version
  with its number, not a save). Then the game is shut down and loaded in
  place (`App.game`: the game holds pointers into itself, so it is never
  copied); a save that passes the hash but is refused further on (edited by
  hand) leaves the title screen. The view goes to the first settler, the
  game paused.
- **Autosave**: when the day of the game changes (`tick / TICKS_PER_DAY`),
  counted from the day a game began or was loaded; it costs a frame (90 ms on
  a 256 × 256 map).
- **Tests**: `test_save` (INFO read back, `save_check()` on damaged saves),
  `test_save_files` (names and numbers). The windows were checked by hand.

## 8. Steps

1. **S1, the core (done).** The serializer, every module's `_serialize()`,
   `game_save()` and `game_load()`, the tests of §5. Measured: the time to
   save and load a 256 × 256 game, the file's size.
2. **S2, the frontend (done).** The menus, the files, quick save and
   autosave, errors told to the player.
3. Later: a name typed for a save, a picture of the colony in the list,
   migrations between versions (with the first public release), saving on
   another thread if large maps make the autosave hitch.
