# Fibres, cordage and containers

*Design document. Nothing here is implemented yet: §10 gives the steps, the
ROADMAP says what has landed. Values marked *(tuning)* are first guesses, to
adjust in play.*

Containers (baskets, bark boxes, net bags, pack baskets) are the first things
the colony makes that are worth more than their materials: they let a settler
carry many small things in one trip, and keep food. They are also the first
long chains of the game, made the way people of the Upper Palaeolithic and
the Mesolithic made them: fibres cut from the right plant at the right time of
year, dried, retted, scraped and twisted into cord with tools that were made
for it and wear out, each step's quality passing to the next.

Related: `docs/KNOWLEDGE.md` (knowledges, quality, tools, recipes),
`docs/CARRYING.md` §8 (containers as places), `docs/PLANTS.md` (products and
seasons), `docs/TREES.md` (species), `docs/WATER.md` §7 (the moisture of
items).

## Contents

1. Principles
2. Time: real work, compressed waiting
3. The plants and what they give
4. The chains, step by step
5. Tools
6. Quality from the plant to the basket
7. The containers
8. Knowledges
9. What changes in the engine
10. Steps
11. Later

---

## 1. Principles

- **As in real life.** Every material comes from a plant of the game, taken
  when people really took it, prepared the way they did. Nothing is made from
  nothing, and nothing is quick: a net bag is days of work, most of it making
  the cord.
- **Every stage is an item**, never weighed to the gram: an item stands for
  a handful or a bundle, as *Blackberries* stands for a handful of berries.
  Stems, fibre, cord, bark, rods: each lies in stockpiles, rots or dries,
  and is hauled like any other item.
- **The season matters.** Most materials can only be taken part of the year;
  out of season they are worse or not there at all. The first year of a game
  starts in August: nettles and cattails then, birch bark only the next spring.
- **Tools are made, used and worn**, and their quality passes into what they
  shape (§6).
- **Waiting is simulated, not skipped**: stems dry on a tile, ret in the
  dew or in shallow water, rods soak; the existing moisture of items
  (`docs/WATER.md` §7) carries most of it.

## 2. Time: real work, compressed waiting

A season lasts 15 days, so **one game day stands for about six real days**,
and the game starts on day 11 of summer, **the beginning of August**
(spring = March to May, summer = June to August...).

- **Work stays in real hours**: twisting five metres of cord takes about as
  long in the game as it would take a person (2,500 ticks = one hour).
- **Waiting is compressed like the calendar** (×1/6): drying, retting,
  soaking, the windows when a material can be taken. Two weeks of dew retting
  become two to three game days. Grass already dries in about three days.

Months to game days (the climate decides in the end: products follow the
warmth plants gather, `docs/PLANTS.md` §7, so a cold year shifts them):

| Real period | Game days |
|-------------|-----------|
| mid May | spring 13 |
| end of May | spring 14 – 15 |
| mid July | summer 7 – 8 |
| beginning of August | summer 11 (the start of a game) |
| end of September | autumn 5 |
| end of October | autumn 10 |
| November to February | autumn 11 to winter 15 |

## 3. The plants and what they give

### 3.1 Plants already in the game

| Plant | Part | When (real / game) | Taken how | What it gives |
|-------|------|--------------------|-----------|---------------|
| **Nettle** | Stems | August – October, after flowering, until the frost (summer 11 – autumn 10) | Cut whole (gloved with leaves, or the stems first left to wilt); the plant grows again from its roots | The finest and strongest fibre: cord, thread, net |
| **Nettle** | Stems standing through winter | November – March | Already retted by the weather, but weathered | Coarser fibre, no retting needed |
| **Cattail** | Leaves | July – September, mature and still green (summer 6 – autumn 5) | Cut near the base; the plant grows on | Plaited and twined baskets, mats, coarse cord |
| **Cattail** | Down (seed heads) | October – March | Picked | Tinder, stuffing (with fire, `docs/KNOWLEDGE.md` §5) |
| **Birch** | Outer bark | End of May – mid July, when the sap runs (spring 14 – summer 7) | One vertical cut, the bark lifts off in a sheet; the inner bark is left so the tree lives; a tree gives bark once (it grows back in 10 to 20 years) | Folded trays, sewn boxes; later buckets, birch tar |
| **Birch** | Winter bark | The rest of the year, not in hard frost | The same, warmed first (fire, hot water) as Ojibwe makers did in late winter: much slower, half the yield, thicker and stiffer | The same, worse |
| **Hazel** | Rods (one-year shoots) | Best November – February, leafless (autumn 11 – winter 15); green in summer, weaker | Cut at the stool with a flake or a hand axe; the stool sends up new rods | Stakes and weavers of frame baskets, rims, hoops |
| **Bramble** | Long canes | Autumn – winter | Cut, thorns stripped, split in four | Binding of coiled baskets (§11) |
| **Grass, reeds** | Stems | As today | As today | Coiled baskets, mats (§11) |

The nettle's young shoots (food in spring and autumn) stay as they are: the
stems are a second product of the same plant.

### 3.2 Two new plants: willow and lime

The two plants the people of temperate Europe used most for baskets and
cord are not in the game yet. They come with this work (`docs/TREES.md`):

| Plant | Shape | Where | Part | When | Gives |
|-------|-------|-------|------|------|-------|
| **Willow** (osier) | Low, many stems like the hazel (2 levels), sparse crown | By the water: shores, wet hollows, river banks (the cattail's neighbours); full light; floods do not harm it | Rods (one-year shoots) | Best November – February; green in summer | The best basket rods: longer and more pliable than hazel; bark peeled in spring (strips) |
| **Lime** (small-leaved lime, *Tilia cordata*) | Tall tree, dense crown, shade tolerant | Rich soil in mixed woods, with beech and oak; uncommon (about 5 % of trees) | Bast (inner bark) of young poles and coppice shoots | Peeled when the sap runs (May – June, spring 13 – summer 3), then retted in water | Bast strips: the lacing of bark boxes (Ötzi's), and cord: the great fibre of the Mesolithic (fishing nets) |

**Lime bast without felling.** Felling needs an axe and the Felling knowledge,
which the colony does not have. Lime bast came from young poles and coppice
shoots, which a hand axe or a chopper can cut: a **young lime** (growth below
0.3, a sapling or a shoot from a stool) can be cut for poles without the
Felling knowledge (*Cut poles* job, Gathering). A cut lime stool sends up new
shoots *(tuning)*.

Willow bark (salicin, a painkiller) and lime flowers (a tea) are noted for
health (M6).

## 4. The chains, step by step

Each line is a step done by a settler (work in real hours, *tuning*), or a
wait (compressed, §2).

### 4.1 Nettle cord

| Step | Item before | Item after | Work / wait | Tools | Knowledge |
|------|-------------|------------|-------------|-------|-----------|
| Cut stems (gathering zone) | nettle (plant) | **Nettle stems** (a bundle of about 20 stems) | 0.1 h a bundle; a grown nettle gives 3 | flake best, hand axe, chopper; bare hands ×0.5 (and stung) | Gathering 50 |
| Dry | fresh stems | dry stems | about 2 game days on a tile out of the rain (the existing drying) | — | — |
| Ret (optional) | dry stems | retted stems | dew retting: lying out in the open, on the ground, wet from rain and dew, 2 to 3 game days; water retting: lying in shallow water, about 1.5 game days (§4.4) | — | — |
| Break, peel and scrape | stems | **Nettle fibre** (a handful, about 20 g) | 1 h a bundle unretted, 0.5 h retted | a cobble to break the stems on a log, a **wooden scraper** (§5) | Cordage 50 |
| Twist (reverse wrap) | fibre | **Nettle cord** (a hank of 5 m, two-ply) | 0.75 h a hank; a handful of fibre makes 2 | bare hands | Cordage 50 |

A metre of cord takes about 15 minutes from dry stems, a little less once
retted; the 40 m of a net bag take about 10 hours before the first loop is
made. The Wala of Papua New Guinea spent more than half of their making
time on string: the game should feel it.

Later: **fine thread** (Cordage 70, three times the work per metre) for
sewing and fine nets.

### 4.2 Lime bast

| Step | Item before | Item after | Work / wait | Tools | Knowledge |
|------|-------------|------------|-------------|-------|-----------|
| Cut poles (gathering zone) | young lime | **Lime poles** (a few poles) | 0.3 h | hand axe, chopper | Gathering 50 |
| Peel | poles, in the sap season | **Lime bark** (strips with their bast) | 0.3 h | flake | Cordage 50 |
| Ret in water | bark | retted bark | about 5 to 8 game days in shallow water (4 to 8 weeks in reality: the layers come apart) | — | — |
| Separate the layers | retted bark | **Lime bast** (a bundle of strips) | 0.5 h | bare hands | Cordage 50 |

Bast strips lace bark boxes as they are, or are twisted into cord like
nettle fibre (coarser, faster: 0.4 h a hank).

### 4.3 Bark, rods and leaves

| Step | Item | Work / wait | Tools | Knowledge |
|------|------|-------------|-------|-----------|
| Peel birch bark (gathering zone) | **Birch bark** (a sheet); a grown birch gives 4 *(tuning)*, an old one more | 0.2 h a sheet in season; out of season ×4 and half the sheets | flake | Gathering 50 |
| Soften bark (before working it if it dried) | the sheet, pliable again (wet) | lying in shallow water, about half a game day (warm water or a fire's heat do it faster: later) | — | — |
| Cut rods (gathering zone) | **Hazel rods**, **Willow rods** (a bundle of about 20) | 0.3 h a bundle | flake, hand axe, chopper | Gathering 50 |
| Soak dried rods | rods pliable again | rods lying in shallow water, about half a game day | — | — |
| Cut cattail leaves (gathering zone) | **Cattail leaves** (a bundle) | 0.1 h | flake best, bare hands ×0.6 | Gathering 50 |
| Dry leaves | dry leaves | about 2 game days, in the shade at best | — | — |

Rods and leaves are woven **green** (fresh, within a few days of cutting) or
**dried then dampened**. Rods that dried must be soaked; leaves are dampened
by the weaver as part of the work *(simplification until water can be
carried)*. Green cattail leaves shrink as they dry: a basket woven green
works loose (quality cap −15).

### 4.4 Retting

A **real step with a quality bonus**, built on the moisture of items:

- A stack of nettle stems (or lime bark) has a **retting progress** (0 to 1
  and beyond), kept like the drying progress (average of the stack).
- It **rets while it is wet** (moisture *wet* or *soaked*) **and warm enough**
  (above 5 °C, faster when warm *(tuning)*):
  - **dew retting**: stems lying in the open on the ground (in a stockpile
    or not), wet from rain, and from the dew on nights of high air humidity
    (a new source of wetting, small, at night);
  - **water retting**: stems lying in shallow water (soaked within the hour,
    as items in water are today): twice as fast, but the water smells
    (a mood malus nearby once smells exist).
- Stems under a roof stay dry and do not ret: the player chooses.
- **Well retted** (progress 1, about 2 to 3 game days of dew): the fibre comes
  away easily (half the scraping work) and is finer: **quality cap +15**.
- **Over retted** (beyond 1.5): the fibre itself starts to rot: the bonus
  turns into a malus, −10 per 0.25 beyond *(tuning)*. Stems must be taken in
  (dried) in time.
- The tile card and the item's tooltip show it: *Retting: 70 %*, *Retted*,
  *Over retted*.

## 5. Tools

| Tool | New? | Made of | Recipe | Used for | HP | Notes |
|------|------|---------|--------|----------|----|-------|
| Sharp flake | exists | flint | exists | cutting stems, leaves, rods, bark; trimming | 15 | Too sharp to scrape fibre: it cuts it |
| Hand axe, chopper | exist | flint, cobble | exist | cutting rods, poles | 120, 80 | |
| Cobble | exists | — | — | breaking nettle stems on a log | 200 | |
| **Wooden scraper** | new | branches | crafting spot, flake, 0.3 h, Cordage 50 | scraping fibre (a dull edge cleans the bark off without cutting the fibre) | 40 | |
| **Flint borer** | new | a sharp flake | crafting spot, cobble, 0.2 h, Stone knapping 60 | piercing bark to sew it | 20 | A flake retouched to a point; borers are known from the Lower Palaeolithic |
| Bone awl, eyed needle | later | bone | Bone working | sewing hide, finer bark work | | With butchering and Sewing (§11) |
| Netting needle, mesh gauge | later | wood | | knotted nets | | The first net bag is looped with the fingers |

New kinds of work (`ToolUse`) and their speed factors *(tuning)*:

| Work | Bare hands | Flake | Hand axe | Chopper | Wooden scraper | Flint borer | Cobble |
|------|-----------|-------|----------|---------|----------------|-------------|--------|
| Cutting stems and leaves | ×0.5 | ×1.0 | ×0.9 | ×0.7 | — | — | — |
| Cutting rods and poles | — | ×0.6 | ×1.0 | ×0.9 | — | — | — |
| Peeling bark | ×0.3 | ×1.0 | ×0.8 | ×0.6 | — | — | — |
| Scraping fibre | ×0.3 (fingernails) | ×0.6, quality cap −10 | — | — | ×1.0 | — | — |
| Piercing bark | — | ×0.4 (its corner) | — | — | — | ×1.0 | — |
| Weaving, twisting, looping | ×1.0 | — | — | — | — | — | — |

Tools wear as today: one hit point per hour of work. A borer's point lasts
about 20 hours of sewing.

## 6. Quality from the plant to the basket

Today the quality of a made thing comes from the maker's level, capped at the
average quality of the materials + 20 (`docs/KNOWLEDGE.md` §2.2), and the
tool's quality only speeds the work. With this work:

1. **Raw materials**: quality from the gatherer's level (as today), times the
   **plant's state**: `0.7 + 0.3 × growth` and its health (a plant
   suffering from drought or frost gives poorer fibre), and the **moment**:
   in the middle of its window ×1, at its edges −10, out of season (winter
   bark, winter nettle stems) −25 *(tuning)*.
2. **Each step's cap** = the average of the materials and of the **tool that
   shapes them**, the tool counting for a quarter, + 20. A master with good
   fibre but a poor scraper gets less out of it. Tools that only cut
   (cutting rods) do not count. Steps add their own bonus or malus: retting
   (+15 or worse, §4.4), green leaves (−15), a flake used as a scraper (−10).
3. **The product**: the same rule with the prepared materials (the cord, the
   bark, the rods): a box sewn with poor cord is a poor box.
4. **Quality gives the hit points** (×0.6 at quality 0, ×1 at 50, ×1.5 at
   100, as today) and the wear (§7.3).

Example: a gatherer at 70 cuts nettle stems from a tall healthy plant in
September (stems ≈ 55); they ret well (+15); a cordage maker at 80 scrapes
them with a scraper of quality 60 (fibre cap `(55 × 3 + 60) / 4 + 20 + 15`
≈ 91, level gives ≈ 67: fibre 67); twisted by the same (cord ≈ 67); a basket
maker at 75 weaves a net bag from it (≈ 60, capped at 87): a net bag of
quality 60, about 1.05 × its base hit points.

## 7. The containers

### 7.1 List

Work counts everything from the plant for a maker at level 75 (speed ×1),
waits apart *(tuning)*. Volumes in litres (bulk = 2 per litre,
`docs/KNOWLEDGE.md` §2.1).

| Container | Materials | Tools | Work | Holds | Carried | Weight | Knowledge |
|-----------|-----------|-------|------|-------|---------|--------|-----------|
| **Folded bark tray** | 1 sheet of birch bark, 2 handfuls of twigs (pins) | flake | 0.5 h (+ 0.2 h the bark) | 2 L, open | in a hand | 0.1 kg | Basketry 50 |
| **Plaited cattail basket** | 3 bundles of cattail leaves | flake | 5 h (+ 0.3 h cutting, 2 days drying) | 10 L, open | in a hand | 0.4 kg | Basketry 55 |
| **Looped net bag** | 8 hanks of nettle cord (40 m) | fingers | 8 h (+ about 10 h of cord) | 10 L, slung; not berries (crushed) | slung | 0.1 kg | Basketry 60 |
| **Frame basket** (hazel or willow) | 3 bundles of rods | flake or hand axe | 8 h (+ 1 h cutting); willow ×0.8 work, cap +10 | 20 L, open | in a hand | 1.0 kg | Basketry 60 |
| **Sewn birch bark box**, with lid (as Ötzi's) | 2 sheets of birch bark, 1 hank of nettle cord or 1 bundle of lime bast, 1 bundle of rods (rim) | flake, flint borer | 4.5 h (+ about 1.5 h materials) | 3 L, closed | in a hand, at the belt later | 0.2 kg | Basketry 65 |
| **Pack basket** | 6 bundles of rods, 2 hanks of cord (straps) | flake or hand axe | 14 h (+ 2 h rods, 2.5 h cord) | 40 L, open | **on the back** | 2.0 kg | Basketry 70 |

About 5 hours for a log basket by a professional basket maker today, about
3 hours for a first cattail basket from ready leaves (sources below): the
figures above are of that order for a stone age maker at 75.

### 7.2 What they do

- **Carrying** (`docs/CARRYING.md` §8): a container takes a place (a basket
  in one hand, a net bag slung, a pack basket on a new **back** place) and is
  a place itself, a **sub-place** with its own room (its volume) and weight
  limit. The hand holding a basket holds nothing else, but the basket holds
  berries, nuts, roots, small bundles (twigs, fibre, cord). A settler with a
  pack basket brings back from a bramble patch ten times what fits in cupped
  hands. Haulers and gatherers take a container when the trip is worth it
  (many small items), as they take a tool.
- **What fits**: loose and small items (form `LOOSE`, small `BUNDLE`s); not
  rigid long things (branches, spears), not liquids (until a sealed box,
  §11). The net bag crushes soft fruit (berries are refused).
- **Keeping**: a closed bark box protects what is in it; birch bark holds
  **betulin**, which keeps food from moulding: food rots at ×0.7 in a birch
  bark box *(tuning)*. Bark boxes and baskets also serve as **storage** in a
  stockpile (§11).
- **Embers**: a bark box can carry embers wrapped in leaves, as Ötzi's did,
  to light a fire elsewhere without friction (with fire, later).

### 7.3 Wear and death

- Containers have hit points (from quality, §6) and lose them by **use**: one
  point per hour carried with a load *(tuning)*, more when overfilled.
- Plant matter **rots when kept wet** (the existing rot by moisture: almost
  nothing when dry, ×1 soaked): a basket left out in the rain for days
  falls apart; birch bark hardly rots (×0.2).
- At 0 hit points the container breaks; what was in it falls on the tile.
- Mending (a new rim, patched weave) comes later (§11).

## 8. Knowledges

Two new knowledges, both **starting knowledges for now** (rolled 50 to 80
like the seven others), to test the chains; balance later (maybe only some
settlers know Basketry):

| Knowledge | id | Covers | Influenced by (related practice) |
|-----------|----|--------|----------------------------------|
| **Cordage and fibres** | `cordage` | preparing fibre (scraping, separating bast), twisting cord, the wooden scraper | Gathering (cutting stems), Basic shelters (binding) |
| **Basketry** | `basketry` | trays, baskets, net bags, bark boxes, pack baskets | Cordage, Gathering |

Cordage is very old: a three-ply cord fragment made by Neanderthals is about
50,000 years old (Abri du Maras, France). Both are in the first era of the
project owner's technology outline (Cordage and fibres, then Basketry).
Cutting stems, leaves, rods, bark and poles is Gathering; retting needs no
knowledge (it is lying in the dew); the flint borer is Stone knapping.

## 9. What changes in the engine

What the game cannot do yet, and must for this work:

1. **Recipes with several ingredients** of different types (bark + cord +
   rods), each with its count and alternatives (cord *or* lime bast), and
   several products (fibre makes 2 hanks). `RecipeDef` today takes one
   input type (with one alternative), and a craft holds one stack
   (`Workstation.work_input`). With several:
   - `RecipeDef.ingredients[]`: up to 3, each with its types (two
     alternatives), count and moisture (§9.2);
   - a settler still carries **one load at a time** (`docs/CARRYING.md`):
     the crafter fetches the kinds one after the other and **lays each in the
     station's work zone** (on the station's tile), where `zone_take()`
     already finds ingredients; what lies there counts in the colony's stock,
     so haulers leave it;
   - the craft starts once every kind lies in the zone or in hand: the station
     takes them all (`work_input` becomes one stack per ingredient, saved:
     `SAVE_VERSION` raised);
   - the bill's allowed types (`Bill.inputs`) cover every ingredient's types;
     the bill window lists the ingredients, each with what it lacks.
2. **Inputs in a given state**, from the moisture every water-taking item
   already has (`ItemStack.moisture`, `docs/WATER.md` §7): hearth tinder and
   structure thatch are already fetched dry only (`hearth_ingredient_dry()`,
   `structure_material_dry()`). Recipes get the same, per ingredient: **dry**
   (below the type's `dry_below`: stems to scrape, leaves to weave),
   **green** (still near its fresh moisture: rods and leaves woven at once,
   bark worked fresh), **wet** (rods soaked, dried bark softened). The bill
   fetches only fitting stacks and says why it waits ("rods too dry: soak
   them").
3. **The tool in the quality cap** (§6), and kinds of work of their own
   (§5).
4. **A second product per plant with its own harvest**: nettle shoots are
   picked (the plant stays), nettle stems cut whole (the plant grows again);
   cattail rhizomes dug, leaves cut, down picked. Today the harvest mode is
   the plant's.
5. **Products of trees**: birch bark (once per tree: the tree is marked
   *peeled*, shown on its trunk), hazel and willow rods (a stock growing back
   in a year), lime poles from young limes.
6. **Retting progress** on item stacks (§4.4), and dew as a small source of
   wetting.
7. **The quality of raw materials from the plant's state and the moment**
   (§6.1).
8. **Containers as sub-places** in carrying (`docs/CARRYING.md` §8): room,
   weight, what fits, the back place, taking a container for a trip, contents
   dropped when it breaks.
9. **Two new species**: willow and lime (`docs/TREES.md`: shapes, sprites,
   world generation, `tree_species_weights()`), with their needs.
10. **Two new knowledges**, starting (§8).

## 10. Steps

In the order agreed with the project owner: nettle cord first, then the bark
tray and the cattail basket, then carrying in containers; then the sewn box,
rods and the pack basket; then the net bag.

1. **C1 Engine for chains** *(built)*: recipes with several ingredients (laid in the
   work zone one load at a time) and products, ingredients in a moisture
   state (`ItemChoice.dry` grows into a wanted state: dry, green, wet), the
   shaping tool in the quality cap, the new kinds of work; the two
   knowledges (starting); the wooden scraper and the flint borer. The first
   recipe of several ingredients, to test it in play, is the **folded bark
   tray** (birch bark and twigs): the birch bark item comes with it (only
   spawned by the debug tools until C3 brings its harvest). Existing recipes
   (one ingredient) keep exactly what they did: the state hash of
   `bench_simulation` stays the same.
   - *As built*: `RecipeDef.ingredients[3]` (types, count, `ItemMoistureWant`),
     `Workstation.work_inputs[3]` (`SAVE_VERSION` 12); the refactor alone left the
     state hash of `bench_simulation` unchanged (scenarios 1, 3 and 5). The crafter
     lays each load on the station's tile and starts once the zone holds every
     ingredient (`missing_ingredient()`, `start_from_zone()` in `core/colonist.c`);
     haulers leave those items while the station is worked at
     (`workstation_gathers()`). "Wet" (`ITEM_WANT_WET`) is "not dry": pliable,
     fresh or softened again; the tray wants its bark so. `TOOL_USE_CUTTING`,
     `TOOL_USE_SCRAPING`, `TOOL_USE_PIERCING`; the tray is cut with a flake (which
     shapes it), the scraper made from a branch with a flake or a hand axe
     (shaping it, Cordage 50), the borer retouched from a sharp flake with the
     hammer stone (Stone knapping 60). The tray is in the Tools category until
     containers have one (C4). Cordage and Basketry are rolled from a stream of
     their own (`GAME_LATER_KNOWLEDGE_SEED`), so that adding them changed nothing
     else in a game.
   - *Found on the way*: `ItemTypeMask` was 64 bits and the game had 50 item types;
     the items of C2–C7 (about 14) would have brought it to the limit. *Done*: a set
     takes as many 64-bit words as the types need (`ITEM_TYPE_WORDS`), static tables
     list types (recipe tools, counted resources), so the number of types has no limit.
2. **C2 Nettle cord**: nettle stems (a second product, cut whole, its season),
   drying (existing), retting (progress, dew, shallow water, over retting),
   the wooden scraper, nettle fibre, nettle cord; raw quality from the
   plant's state and the moment.
3. **C3 Bark tray and cattail basket**: birch bark (window, winter bark, the
   tree peeled), softening in water (the folded tray is already there); cattail leaves
   (drying, green weaving), the plaited basket.
4. **C4 Carrying in containers**: sub-places, the back place, what fits,
   taking one for a trip, wear and rot, contents dropped when broken; the
   Gear tab shows contents (`docs/CARRYING.md` §8).
5. **C5 Rods, sewn box, pack basket**: hazel rods (a stock, green or dry,
   soaking in shallow water), the flint borer, the sewn bark box, the frame
   basket, the pack basket on the back.
6. **C6 Willow and lime**: the two species (shapes, sprites, world
   generation), willow rods, young lime poles, lime bark retted in water,
   lime bast (lacing, cord).
7. **C7 Net bag**: looped from nettle or lime cord.

Each step ends with a playable build, its unit tests, the translations
(en, fr), and this document marked as built.

## 11. Later

- **Storage in stockpiles**: baskets and boxes as storage in a stockpile
  (more held on a tile, food kept in bark at ×0.7 rot).
- **Birch tar** (Hafting, `docs/KNOWLEDGE.md` §13): sealing seams, so a bark
  box or bucket holds **water** (carrying water, boiling with hot stones);
  the seam's quality decides whether it leaks.
- **Sinew thread** (with butchering): dorsal and leg tendons, dried, pounded,
  shredded, moistened to use; it shrinks as it dries and tightens the seam.
  **Bone awl and eyed needle** (Bone working, Sewing): hide bags, sewn
  clothes, a skin water bag.
- **Coiled baskets**: grass or reeds bound with split bramble: very slow
  (20 to 40 h), very strong.
- **Mats** (cattail, reeds): floors and walls of shelters, bedding.
- **Fine thread** and **knotted nets** (netting needle, mesh gauge): fishing
  and hunting nets (Traps).
- **Mending** containers; dyes; decorated bark (winter bark scraped in
  patterns).
- **Dead birches**: their bark outlives the wood, brittle: tinder, not boxes.

## Sources

- Ötzi's birch bark containers, sewn with lime bast:
  <https://www.iceman.it/en/equipment/>
- A Mesolithic birch bark container from Friesack:
  <https://blogs.fu-berlin.de/interactionlab/?p=75>
- Birch bark folded and sewn with split root, holes made with an awl:
  <https://naaer.hoodmuseum.dartmouth.edu/northeast-woodlands/economics/work-2>,
  <https://www.artsmia.org/surrounded-by-beauty/northeast/algonquin_home.html>
- Peeling birch bark, leaving the inner bark:
  <https://gardeningknowhow.com/ornamental/trees/birch/birch-bark-harvest.htm>
- Nettle fibre (season, labour; about 1 g of good fibre a stem):
  <https://www.researchcatalogue.net/view/3093081/3322214>,
  <https://sallypointer.blogspot.com/2021>
- Neolithic cordage of lime bast and nettle (La Draga):
  <https://ddd.uab.cat/pub/artpub/2023/7df72699be6e/1-s2.0-S2352409X23002675-main.pdf>
- String and cordage in the Stone Age (Wala string making, Neanderthal cord):
  <https://smithsonianmag.com/science-nature/archaeologists-are-reconstructing-the-invisible-technologies-of-the-stone-age-from-rope-to-thread-and-twine-180989534>,
  <https://nautil.us/rope-twine-and-thread-invisible-technologies-of-the-stone-age-1284982>
- Basket making times (a log basket in about 5 h; a first cattail basket in
  about 3 h; soaking willow and seasoning hazel):
  <https://www.irishtimes.com/culture/art-and-design/hands-on-1.583948>,
  <https://buymeacoffee.com/manituokahas/cattail-basket?l=es>,
  <https://morningchores.com/how-to-weave-a-willow-basket/>
