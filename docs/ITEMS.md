# Items, one by one

Design of the move from stacks to individual items (under way). Today a tile
holds one stack of one type whose quality, hit points and rot are the average
of its items, and twigs, branches, cobbles and flint lying about are counts in
the map (`Map.twigs`...), not items. After the move, every item is a thing of
its own, from the stone on the ground to the spear in a hand. Values marked
*tuning* are first guesses.

## 1. What changes for the player

- **Every item is itself.** It keeps its own quality, hit points, rot,
  drying, what was eaten of it. Items are never merged into averages again.
- **Twigs, branches, cobbles and flint are items** like the others, lying
  here and there from the start. Gathering them is picking them up.
  Cobbles become about 50 times fewer than the stones counted today (about
  5 000 on a 200 × 200 map instead of 235 000), twigs and branches somewhat
  fewer *(tuning)*; flint nodules as today.
- **Nothing is stacked by nature.** Items lie apart, as they fall. Several
  items on a tile (a heap on a stockpile, a pile of branches) are simply
  several items there: a click on the tile opens the list of them, sorted by
  type, quality, hit points or rot.
- **A tile may hold items of several types** (a branch and three twigs under
  a tree), up to its room (`ITEM_TILE_BULK`, as today).
- **Items lie anywhere on their tile, turned any way.** Dropped, an item
  takes a spot of the tile away from the items already there, and a random
  angle (any angle: rotated pixel art is allowed to look rough).
- **Bills choose their ingredients.** Each bill has a quality range for its
  ingredients (a slider with a minimum and a maximum). Cooking takes the
  ingredients that would rot first.
- **The known world keeps a copy of every item seen** (`core/known.h`): what
  the colony remembers of a tile out of view is the very items it saw there,
  with their quality, condition, spot and angle at the time.
- **Debug: moving items by hand.** A debug tool picks an item under the
  mouse, drags it anywhere on the level shown and drops it, at the very spot
  of the mouse; while held or hovered, it can be turned.

## 2. Where an item is: a tile, plus a spot for the eye

An item stays **on a tile** for the simulation: paths, reach, storage, roofs,
rain, temperature, the fog of war, reservations and the room of a tile are all
by tile, and nothing in the rules needs finer positions. Free positions would
need a spatial index of their own and rewriting every search, for no rule
that uses them.

Its **spot in the tile** (an offset, 1/256 of a tile each way) and its
**angle** (1/256 of a turn) are for the eye only. They are part of the
state (the known world copies them, a save keeps them) but no rule reads
them. They are drawn from a hash of the item and the tick, not from a random
stream, so that the way items are drawn never changes what happens.

Choosing a spot: a few candidate spots in the tile (deterministic from the
hash), the one farthest from the items already there wins; a crowded tile
lets items overlap (a heap).

## 3. Data

- **`Item`** (about 32 bytes): type, quality, hit points, progress (drying
  or rot), eaten, corpse; where it is (`ItemPlace`: on the ground, carried,
  a tool, delivered to a site, on a workstation, a hearth's fuel...) with its
  tile or its holder; spot and angle; flags (forbidden); the link to the next
  item of the same place.
- **The item pool**: a fixed array grown on demand, items named by
  generational ids (like settlers: an id of an item gone is told apart), a
  free list. About 1 MB for 35 000 items.
- **Per tile**: the first item of the tile (4 bytes a tile, about 3 MB on a
  200 × 200 × 18 map); the items of a tile are a list in the order they
  came (deterministic). The room taken is the sum of their bulk.
- **Per region of 16 × 16 tiles and level**: the mask of the types lying
  there, and counts. Searches for the nearest item of a type skip the regions
  without it, as searches skip empty levels today.
- **Totals per type**, as today (`Items.totals`).
- **The items that change** (food that rots, fresh grass and wet twigs that
  dry, dry twigs out in the rain) are kept in a list of their own: only they
  are brought up to date each hour, never the stones.
- **Holders keep item lists**, not stacks: a settler's load, its tools, a
  site's delivered materials, a workstation's input, a hearth's fuel. Moving
  an item from one place to another changes its place and links; it is never
  copied into an average.

## 4. Performance

Measured on the same machine, one build after the other
(`bench_simulation`, `headless-release`; the processor time of a run varies by
a few percent with the load of the machine, so compare runs made together):

| Run | Before | Step 1 | Step 2 | Step 3 |
|---|---|---|---|---|
| 12 settlers, 4 days, gathering (`12 4 1`) | 3.29 – 3.38 s | 3.31 – 3.36 s | 3.40 – 3.42 s (before: 3.30 – 3.34) | 3.05 – 3.12 s (before: 3.01 – 3.04) |
| the same with the fog of war (`12 4 1 200 42 - fog-revealed`) | 35.5 – 36.9 s | 34.7 – 37.4 s | | 32.5 s (before: 34.1) |

Targets:

- **Memory**: a few MB more (the pool, the tile heads, the known copy).
- **Simulation**: tick times within 5 % of the baseline. Only the items that
  change are updated; searches use the region masks.
- **Drawing** (`--bench-render`): one sprite per item, rotated, in the
  batches the renderer already makes; only visible tiles. When zoomed far
  out, small items (twigs, cobbles) give way to one mark per tile if the
  frame time asks for it.
- **State hash**: hashes every item (tests and benchmarks only).

## 5. Steps

Each step is tested, measured against the baseline, and leaves the game
playable.

1. **The item store** (done). `Items` holds the pool and a list per tile;
   the old stack per tile is now a summary of the items there (the newest
   item's type, the count and average condition of its type), so the callers
   are unchanged. A stack added becomes that many items of its condition;
   items are taken newest first (`items_take()` says what was taken); each
   item rots, dries and gets wet on its own, so a tile may hold dry and wet
   twigs for a while; dropped items get a spot and an angle. A stamp per tile
   tells the known world which tiles changed (it used to copy every tile in
   view). The state hash covers every item. Not yet: the region masks and
   the list of changing items (wanted once litter is made of items, step 3).
2. **Loads** (done). What a settler carries is the very items it picked up
   (`ItemStack.held` names their list in the pool; `items_pick_up()`,
   `items_put()`, `items_drop_load()`, `items_use_up()`): hauled to a
   stockpile, each keeps its condition. Tools are single items already.
   Materials delivered to a site, a workstation's input and a hearth's fuel
   are used up there; they stay counts (returned with their average if the
   site or bill is cancelled). Several types on a tile come with the drawing
   of items one by one (step 4).
3. **Litter as items** (done). World generation still lays stones,
   branches and twigs in map layers (50 times fewer cobbles, two days of
   shedding instead of three); `game_init()` turns them into **wild items**
   (`ITEM_FLAG_WILD`) and the map's litter functions work on those items from
   then on (`Map.litter`), so trees shed items. Wild items stay out of the
   tile's stack: haulers and searches leave them alone, as they left the
   litter, and only gathering picks them up, the very items (the best stone
   of the tile first); then they are items like the others. The gatherer's
   skill now only sets the speed: what is picked up is as good as it lay.
   Wild branches and twigs crumble away (60 and 20 days), each tile keeps
   its ceiling, so the count stays bounded: on a 200 × 200 map, 15 600 items
   at the start (4 800 cobbles, 8 400 flint, 500 branches, 2 000 twigs),
   33 000 after a month, 37 000 after two. Hazelnuts stay a map layer.
4. **Spots, angles and drawing.** Items drawn one by one at their spot and
   angle; the far zoom mark if needed; `--bench-render` measured. The debug
   tool to drag, drop and turn items.
5. **The pile window and the bills.** The list of a tile's items, sortable;
   the ingredient quality range of each bill; cooking by rot.
