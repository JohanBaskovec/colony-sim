# Items, one by one

Design of the move from stacks to individual items (done but for the last
points of §3). Before it, a tile held one stack of one type whose quality,
hit points and rot were the average of its items, and twigs, branches,
cobbles and flint lying about were counts in the map (`Map.twigs`...), not
items. Now every item is a thing of its own, from the stone on the ground to
the spear in a hand. Values marked *tuning* are first guesses.

## 1. What changes for the player

- **Every item is itself.** It keeps its own quality, hit points, rot,
  drying, what was eaten of it. Items are never merged into averages again.
- **Twigs, branches, cobbles and flint are items** like the others, lying
  here and there from the start: hauled, forbidden and moved like any, used
  where they lie (step 9).
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
- **Each item is forbidden and carried elsewhere on its own**, from its
  window, or every item of a tile at once, from the tile's (step 10).
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
  copied into an average. (Done for loads, craft inputs and site materials;
  a hearth's fuel stays kilograms, burnt as it goes, and tools are single
  items kept as stacks of one: step 8.)

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
   then on (`Map.litter`), so trees shed items. (Step 9 made them ordinary
   items.) Wild items stayed out of the
   tile's stack: haulers and searches leave them alone, as they left the
   litter, and only gathering picks them up, the very items (the best stone
   of the tile first); then they are items like the others. The gatherer's
   skill now only sets the speed: what is picked up is as good as it lay.
   Wild branches and twigs crumble away (60 and 20 days), each tile keeps
   its ceiling, so the count stays bounded: on a 200 × 200 map, 15 600 items
   at the start (4 800 cobbles, 8 400 flint, 500 branches, 2 000 twigs),
   33 000 after a month, 37 000 after two. Hazelnuts stayed a map layer
   until `docs/TREES.md` step 1: now they are items too.
4. **Spots, angles and drawing** (done). Items are drawn in vector shapes;
   every type is drawn once into a texture of icons (64 pixels each), and
   each item is a textured quad at its spot, turned by its angle, its size
   by its bulk (`item_drawn_size()`), in batches of their own. A tile shows
   its newest ten items, one when tiles are under 10 pixels wide.
   `--bench-render` shows no difference (frame and drawing times within a
   few percent either way, on the virtual display). The debug panel's
   "Move items by hand" tool (`APP_DEBUG_MOVE_ITEM`): an item pressed on
   (`items_at_point()`) follows the cursor onto the very spot of any tile
   someone could stand on (`game_debug_move_item()`), the wheel over an item
   turns it (`game_debug_turn_item()`); items are named across ticks by
   `ItemId` (slot and generation). The tool stays on until it is turned off
   (one item after the other), grabs what is seen under the cursor (on the
   level shown, or on the ground seen below it when that level is open air)
   and drops it on the ground seen there (`game_build_level()`); no selection
   rectangle is drawn or made while a tool that does not drag out an area is
   on (`tool_drags_area()`).
5. **The pile window and the bills** (done). The inspector of a tile with
   items lists them all (up to 40 lines, then how many more), sorted by
   newest, quality, condition or rot (`items_list()`, `ItemOrder`), wild
   items dimmed. Each bill has a quality range for its ingredients (a slider
   with a minimum and a maximum, `workstation_set_bill_quality_range()`):
   heaps count for their items in range only and only those are picked up
   (`ItemChoice`, `items_pick_up_chosen()`). Ingredients that rot are taken
   soonest to rot first: the settler looks around the station for the one
   that rots away soonest and fetches from the nearest heap holding one
   within six hours of it *(tuning)*, falling back on any heap in range.
6. **Several types on a tile** (done). Items of any types share a tile, as
   long as their bulk fits (`items_room()`). The items of a type form a
   group; the tile's summary is the newest group, `Items.mixed` marks the
   tiles with others, and the `*_of()` functions find a group among some
   types. Searches for food, ingredients, tools, fuel and materials see
   every group of a tile and pickups take the type looked for; a hauler
   remembers the type it collects; storage keeps one group per tile (a
   stockpile stays tidy). A floor dug away or a wall built moves every
   group, each item as it was (`items_lift_all()`, `items_move_all()`).
   The inspector names such a tile a heap, with a line per kind. Measured:
   no cost of its own in the profile (the group lookups are well under 1 %
   of the time); the runs differ from before because items now land where
   they are dropped (12 settlers gathering: within 3 %; 64 settlers
   building: 18 % faster). Not yet: the region masks (not needed so far).
7. **Checks** (done). `items_check()` (every item listed once on its own
   tile, summaries, mixed marks, counts per level and type, forbidden tiles,
   every slot of the pool lying, held or free exactly once) and
   `game_check_items()` (the known world's picture too, and every load
   matching its items). `test_item_simulation` runs whole colonies (gathering,
   building, cooking, under the fog of war, among wild animals) and checks
   the items every in-game hour; `bench_simulation` checks them at the end
   (every hour with `BENCH_CHECK_ITEMS=1`) and reports the heaps, what rotted
   and the crafts of the bills. The cooking scenario (`--scenario 5`) runs
   two lit hearths on heaps of food of all ages, some of several kinds.
   Found on the way: settlers searched the whole map again and again for a
   station when none had a bill they could take, and for tinder that lay
   nowhere; a cheap check first made the cooking scenario four times faster
   (244 µs a tick down to 60 µs on 12 settlers over two days, the same
   state hash).
   Measured on 200 × 200, 12 settlers, 4 days, checked every hour: about
   400 heaps of several kinds out of 4 000 tiles with items while gathering
   (two kinds each, three at most); while building, 378 of 609 after eight
   days (grass, twigs and branches dropped together); 2 of 100 while
   cooking; 7 to 16 storage tiles mixed by dropped loads; no inconsistency.
8. **Follow-ups found by the checked runs** (done).
   - *Storage kept tidy.* Storage tiles got mixed by whatever dropped items
     on them: loads put down by settlers who gave up, harvests from a plant
     growing on a stored heap (the gathering zone covers the stockpile),
     products. Items dropped now land first on the tiles a filter accepts
     (`items_set_drop_filter()`; the game sets `haul_drop_filter()`: off the
     stockpile tiles that would not take them as they are), anywhere when
     none within reach has room. 0 storage tiles mixed in every scenario
     (6 to 11 before).
   - *Food eaten before it rots.* A heap is one group whatever the age of
     its items: settlers ate its newest items and left the oldest at the
     bottom to rot, and the search for food about to rot read the heap's
     average. They now eat the items that rot away first
     (`items_soonest_rotting()`, `ItemChoice.soonest_rot`) and see a heap by
     its worst item. And roasting kept the share of life left: a carrot a
     day from rotting (5 % of its 20 days) made a roast with 5 % of 4 days,
     five hours, and the bills take what rots first. A roast now keeps as
     long as the raw root would have, at most its own full life
     (`item_hit_points_lasting()`). Cooking scenario, 4 days: 12 roasted
     carrots of 16 crafts rotted, none now.
   - *Holders keep the items.* The input of a craft in progress and the
     materials delivered to a site are the settler's load handed over
     (`items_load_move()`, `construction_deliver_load()`): given back as
     they were when the craft or the site is called off (before: all at the
     lowest quality delivered, at the average condition), used up when done;
     the lowest quality among a site's materials still caps the structure
     (`items_load_lowest_quality()`). A hearth's fuel stays kilograms: it
     burns as it goes and is never given back. `game_check_items()` counts
     these items too. The cooking scenario's state hash is unchanged.
9. **No wild items** (done). The player saw two kinds of items on the
   ground: wild ones (no move, no forbid, dimmed in the pile window, only
   gathering picked them up) and the others. Now there is one kind:
   `ITEM_FLAG_WILD`, its counts and `ItemDef.litter_days` are gone, and
   `items_add_natural()` lays what world generation and the trees put down
   as ordinary items (each of its own quality, condition 60 to 100 %, as
   much as the tile takes). Gathering zones lost branches, twigs, flint,
   cobbles and the stones group: what lies on the ground is hauled like
   anything else (stockpile filters decide) and bills and builders take it
   where it lies. Nothing crumbles away any more; the trees' shedding stops
   at the ceiling of a tile (`MAP_BRANCHES_MAX`, `MAP_TWIGS_MAX`, wet twigs
   counted, since twigs left in the rain now get wet like any). A growing
   trunk pushes what lies in its way onto the tile beside it, and plants
   spread under loose items: litter stopped neither before. Each item of a heap can also be clicked and selected on its
   own (`game_items_under_point()`, `game_pick_item()`: the topmost first,
   followed when it moves and across the edge of the fog of war), ringed on
   the map, with a window of its own.
   Measured (`bench_simulation`, against the commit before): 12 settlers
   gathering, 4 days, the same time (5.0 s); 64 settlers, 4 days, about
   25 % slower (16.3 s to 20.8 s): the gatherers of the scenario, with no
   twigs left to gather everywhere, search farther for grass and food
   (`covered_tile_holds()`, the path search), while haulers carry about
   1 000 more items. The cooking scenario's stockpile takes food only, or
   haulers fill it with stones first and the roasts rot by the hearth.
10. **One item forbidden or moved** (done). The window of one item has the
   Forbid and Carry elsewhere actions of the tile's items window, for it
   alone. Forbidding is a flag of each item (`Item.forbidden`), which it keeps
   wherever it is carried; forbidden items form groups of their own beside
   the allowed ones (`ItemStack.forbidden`), which the settlers' functions
   skip (`items_get_allowed_of()`, `items_pick_up_of()`, the chosen items),
   and `Items.forbidden` sums them up per tile (some, all). Forbidding a
   tile's items forbids each of them. An item move names its item
   (`ItemMove.item`, an `ItemId`: an item keeps it in a load), and the hauler
   picks up that very item (`items_pick_up_item()`). What a forced haul puts
   down is forbidden, not the whole destination tile. A forbidden item among
   allowed ones carries a small sign of its own. The count drawn in the
   corner of tiles holding several items is gone: every item is drawn, and
   the window lists them. Games with nothing forbidden keep the same state
   hash.
