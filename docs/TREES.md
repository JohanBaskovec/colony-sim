# Trees, woods and fruit

Tree species instead of one generic tree, the wood species carried by
branches, logs and everything made from them, fruit and nuts as ordinary
items, and blocks of foliage that hold what grows in them.

Status: designed; steps 1 and 2 (§9: nuts as items, the species) are implemented. Values marked *(tuning)* are
first guesses. The trees in the volume (trunk, foliage, felling, shedding)
are `core/tree.h`; the plants' life (growth, rest, light, water) is
`docs/PLANTS.md`; items one by one are `docs/ITEMS.md`.

## 1. Principles

- **A species is a plant type.** The one generic tree gave way to seven:
  oak, beech, sweet chestnut, ash, birch, wild apple and wild pear. The hazel
  stays as it is. Each has its own needs (`PlantDef`) and its own shape and
  wood (a `TreeDef`, §3).
- **Realistic, at the game's scale.** One level is 2 m, a tile about 1 m: a
  tall tree has 3 levels of trunk and a crown 2 tiles out (3 for an old one).
  The species differ in proportion: a beech is denser than an ash, an apple
  tree lower than an oak, a birch narrower.
- **The wood species is a material, not an item type.** A branch stays an
  `ITEM_BRANCHES`, with its species in a field of the item (§6). There are 45
  item types out of the 64 a type mask allows: one type per species and per
  item would not fit.
- **Everything is an item.** Hazelnuts, chestnuts, apples and pears are items
  like berries, one by one, with their own quality, hit points and rot (§5).
- **What grows on a tree is in its crown.** Fruit ripens in the foliage
  blocks and dead branches hang there before they fall: a block of foliage
  holds items, listed one by one when selected (§7).
- **Deterministic.** Species, fruit and falls draw from the game's seeded
  random streams; the state hash covers the new fields. The benchmark hashes
  change at each step (a different world), as expected.

## 2. The species *(tuning)*

| Species | Shape | Crown | Growth / day | Logs | Branches / twigs (crown) | Fruit |
|---------|-------|-------|--------------|------|--------------------------|-------|
| Oak | tall, broad, irregular | dense by the trunk, medium further out | 0.020 (slow) | 70 | 8 / 10 | (acorns, later) |
| Beech | tall, domed | the densest: dense 2 tiles out, deep shade | 0.022 | 65 | 5 / 14 | (beechnuts, later) |
| Sweet chestnut | tall, broad | dense | 0.030 | 60 | 6 / 10 | chestnuts |
| Ash | tall, open | light: medium by the trunk, sparse further out | 0.040 | 55 | 5 / 8 | none |
| Birch | tall, narrow (reach 1) | sparse, medium by the trunk | 0.060 (pioneer) | 30 | 3 / 14 | none |
| Wild apple | low (2 levels), rounded, foliage from the first level | dense and medium | 0.030 | 20 | 4 / 10 | apples |
| Wild pear | tall, narrow (reach 1), pyramidal | medium (dense over the trunk) | 0.025 | 30 | 3 / 8 | pears |
| Hazel (unchanged) | low, several stems | as today | 0.040 | 20 | as today | hazelnuts |

The generic tree before them: growth 0.03 a day, 60 logs, a crown of 6
branches and 10 twigs. Shedding (dead twigs and branches falling, `TREE_SHED_*_CHANCE`)
scales by species: oaks drop many dead branches (twigs ×1, branches ×1.5),
birches many twigs (×1.5, ×1), beeches fine twigs (×1.2, ×0.7), fruit trees
little (×0.7, ×0.5), the others ×1 *(tuning)*.

Needs, from what the species are known for *(tuning)*:

| Species | Light | Ground | Water | Cold |
|---------|-------|--------|-------|------|
| Oak | needs light (`light_min` 0.15) | most, fertility from 0.5 | wide, bears flooding (flood days 40) | hardy |
| Beech | grows in shade (0.03) | drained soil, fertility from 0.6 | no waterlogging (max 1.5, flood days 10) | its young leaves fear late frost |
| Sweet chestnut | 0.10 | gravel and soil, poorer ground (from 0.4; sand is too poor) | drained (max 1.4) | wants warmth (`t_min` 7 °C), less hardy (-20 °C) |
| Ash | 0.10 | rich soil (from 0.8) | moist, near water (optimum 0.8 to 1.7) | hardy |
| Birch | full light (0.20) | poor ground (`poor_soil`, from 0.1: sand too) | anything | the hardiest (-40 °C) |
| Wild apple, wild pear | 0.15 | soil, from 0.5 | medium | hardy |

## 3. Crowns

A `TreeDef` per species (`tree_def()`) replaced the `TREE_*` constants that
were the one tree's: the growths at which its trunk rises to two and three
levels (the wild apple and the hazel stop at two), the reach (an old tree's
the same, an ancient one's a tile farther), a **density shift** (beech and
chestnut one notch denser than their distance from the trunk says, ash,
birch and wild pear one thinner, at least sparse; the tile over the trunk
too), its raggedness, its yields and shedding, and whether it can be old or
ancient (oak, beech, chestnut and ash old; oak, beech and chestnut ancient).
The shape still follows from the plant on the foot (`tree_shape()`), never
stored. The crown of a young tree starts on its first level, whatever the
species (a crown that rose as the tree grew would leave foliage behind):
only the old and ancient trees have a bare trunk under it.

The density matters for play as it does today: one walks on dense foliage
only, so an oak or a beech can be climbed out along its branches, an ash or a
birch hardly; and denser foliage casts more shade (the tile light already
follows the foliage density), so little grows under a beech.

Each foliage tile records its tree's species (a byte per tile, written by
`tree_build()`): for drawing it and naming it (*Beech foliage*) without
looking for its tree.

## 4. Where they grow

World generation picks a species for each tree from its site: the forest
share, the edge and clearing shares (as the hazel's), the ground and the
moisture (`tree_species_weights()` in `worldgen.c`, each species then gated
by its fertility and water response) *(tuning)*:

- **Beech**: the heart of woods, on soil and rich soil that is not wet.
- **Oak**: everywhere, most in open woods and at edges; the lone old trees
  in meadows are oaks.
- **Ash**: rich soil and moist ground, along the water.
- **Birch**: sand, gravel and poor ground, edges and clearings; it is the
  first tree to take open ground (it spreads farthest, `spread_range` 6).
- **Sweet chestnut**: groves on sand and soil, well drained; rarer.
- **Wild apple, wild pear**: rare and alone, at edges and in clearings.

In a temperate world, about oak 30 %, beech 25 %, birch 15 %, ash 12 %,
chestnut 8 %, apple 5 % and pear 5 % of the trees (hazels apart); measured
over eight generated worlds: oak 31 %, birch 20 % (the sand's trees are all
birches), beech 19 %, ash 13 %, chestnut 6 %, apple 6 %, pear 5 %. Spreading
(`docs/PLANTS.md` §13) keeps each species where it suits. An old or ancient
tree pushes the young trees touching its trunk aside, and is written into
the map before the others, so that their crowns do not take its room.

## 5. Fruit and nuts as items

Hazelnuts used to lie in a layer of counts per tile (`Map.nuts`), rotting on
average, eaten from the layer by settlers and animals, and gathered by the
zones. They are now items like any other (step 1, done): the layer only holds
what a new world lays, as for branches and twigs, until the game starts:

- what a tree lets fall is laid as items (`items_drop()`), each with its own
  quality, hit points and rot;
- a new world lays its fallen nuts as items, as it does stones and branches;
- fallen fruit is hauled like branches (a stockpile that takes it gets it),
  eaten where it lies by hungry settlers, and by animals that eat it (boars
  and deer eat nuts, chestnuts and fruit on the ground);
- the zones no longer pick up fallen nuts: they will pick fruit in the
  crowns (§7).

New items *(tuning)*:

| Item | Tree | Season | Per grown tree | Weight (kg) | Nutrition | Rots in (days) |
|------|------|--------|----------------|-------------|-----------|----------------|
| Hazelnuts | hazel | autumn | 30 | 0.10 | 0.075 | 90 |
| Chestnuts | sweet chestnut | mid to late autumn (days 4 to 13) | 40 | 0.15 | 0.065 | 30 |
| Apples | wild apple | end of summer (day 12) to mid autumn (day 10) | 40 | 0.20 | 0.025 | 30 (sour, they keep) |
| Pears | wild pear | autumn (days 1 to 12) | 30 | 0.20 | 0.028 | 14 |

At most `MAP_NUTS_MAX` (8) of each lie on a tile. Only the hazelnuts lie in
a layer in a new world: the other fruit only falls in a running game.

That makes 48 item types. Acorns and beechnuts (food for boars, and for
settlers once acorns are leached) come later (§10).

## 6. The wood species on items

### 6.1 Which items carry it

The **logs** (`ITEM_WOOD`) and **branches** carry the species of their tree.
**Twigs** stay generic (fuel and kindling: nobody tells them apart). What is
made from wood carries **every** species it was made from: a thrusting spear
from an ash branch and an oak branch is *of ash and oak*. Hardening a spear
or a digging stick keeps its species. Stone items carry none (wood only for
now).

Items and stacks get a `uint16_t woods` field: a bit per species (8 used, room
for 16), 0 for items without wood. Like the forbidden flag and the body's
record, it keeps groups apart: branches of oak and branches of beech never
share a stack on a tile or in a load. `Item` grows from 24 to 28 bytes.

### 6.2 What the species changes *(tuning)*

Moderate effects, from the woods' known character:

| Wood | Weight | Toughness | Shafts | Weathering | Fuel | Known for |
|------|--------|-----------|--------|------------|------|-----------|
| Oak | ×1.10 | ×1.10 | +0 | ×0.6 | ×1.20 | hard, heavy, lasts outdoors |
| Beech | ×1.10 | ×1.00 | +0 | ×1.5 | ×1.25 | hard, rots outdoors, the best firewood |
| Sweet chestnut | ×0.85 | ×0.85 | -5 | ×0.6 | ×0.85 | light, splits, lasts outdoors (stakes) |
| Ash | ×1.05 | ×1.30 | +10 | ×1.4 | ×1.20 | tough and springy: handles, spears |
| Birch | ×1.00 | ×0.85 | -5 | ×1.6 | ×1.00 | rots fast, catches fire easily |
| Wild apple | ×1.05 | ×1.10 | +0 | ×1.0 | ×1.10 | hard, fine-grained |
| Wild pear | ×1.05 | ×1.05 | +0 | ×1.0 | ×1.10 | hard, fine-grained |
| Hazel | ×0.85 | ×1.15 | +8 | ×1.0 | ×0.90 | straight supple rods: spears, sticks |

- **Weight**: of the item (logs, branches and what is made from them,
  `item_stack_mass()`), so of loads. How many a settler carries stays per
  type, and weapons have no weight in the swing yet (§10).
- **Toughness**: how long a wooden tool lasts (`tool_wear_ticks()`: the
  ticks of use per hit point lost, × the toughness; weapons do not wear in
  combat yet) and the maximum hit points of a wooden structure
  (`structure_wood_max_hit_points()`).
- **Shafts**: quality points added to spears and digging sticks when made
  (not when hardened), still within the recipe's cap
  (`ITEM_QUALITY_CAP_BONUS` over the materials).
- **Weathering**: how fast a structure wears out of doors. Recorded only:
  wooden structures do not wear by themselves yet (only beds pack down,
  §10).
- **Fuel**: how long a log or a branch burns on a fire
  (`hearth_stack_fuel_value()`).

An item of several species takes the mean of their values (the shares of
each are not kept: a bit per species). The values live in `core/wood.h`
(`WoodDef`, `wood_weight()`...).

### 6.3 Names

The species is in the name: *Oak branches*, *Beech wood*, *Ash thrusting
spear*; in French *Branches de chêne*, *Bois de hêtre*, *Épieu en frêne*
(`wood_item_name()`, `hud_item_name()` in the frontend).
Each type that carries a species has a pattern key (`item.branches.of_wood`:
"{0} branches", "Branches de {0}"), and each species a translated noun
(`wood.oak`: "oak", "chêne"); several species are listed with a list key
(`wood.list.two`: "{0} and {1}", "{0} et {1}"): *Épieu en frêne et chêne*.
The seven species and the hazel take no elision in French; species starting
with a vowel (maple, alder) would need a form of their own (§10).

### 6.4 Constructions

A structure keeps the species of its wood: the delivered materials of its
site keep their `woods`, and the built structure records them on its head
(`Map.structure_woods`, `Map.roof_woods` for roofs: a `uint16_t` per tile,
like its quality and hit points; copied into the known map, hashed only
where set). Its toughness gives its hit points
(`map_structure_max_hit_points()`). Its panel and tooltip show them
(*Wood: oak and ash*); names stay those of the type for now (§10).

A load of another species than what a site holds goes in all the same: the
items brought and the items already there all take both species (a
delivered slot is a single load, of a single group), and so does the
structure. Nobody is left holding wood a site will not take. A site
finished at once (debug) is of the species chosen.

**Choosing the species.** A right click on a structure's icon in the build
menu opens a list (`ui_popup_list()`): *Any wood* (the default), then each
species, with how many branches of it lie about (not forbidden,
`wood_count_lying()`). The choice is kept per structure type
(`App.structure_woods`) and goes with the blueprints placed after it
(`game_place_structure_rect_wood()`); the card shows it. A blueprint's
panel has the same choice (an action button showing a branch) and shows
the species brought so far. Haulers then bring only branches of the chosen
species (`ConstructionSite.wood_choice`, a mask, 0 for any), from under
other species on the same tile too. Bills get the same choice for their
wooden ingredients (`Bill.woods`, through `ItemChoice.woods`), a dropdown
in the bill's details. Workstation sites do not record their species yet
(§10).

## 7. Foliage that holds items

### 7.1 Items in the crown

Every block of foliage, dense, medium or sparse, holds items up to a
capacity: dense 200, medium 100, sparse 40 of bulk (a tile of ground holds
`ITEM_TILE_BULK`, 1000) *(tuning)*. What a tree grows there is **attached**:
still on the tree, it does not rot, is not hauled nor eaten, does not count
in the colony's stocks, and stays in medium and sparse foliage, which
anything else falls through as today. Items laid on dense foliage by
settlers lie there as they do now.

What hangs is kept apart from the items of the ground (`core/crown.h`): a
pool of small records (type, quality, hit points, wood species, the day it
came to hang) with a list per tile, reached through `Map.crowns` in a
running game. A flag on the items would have made every user of the items
(hauling, eating, rot, stocks, animals, the heaps drawn) skip attached ones;
a store of its own leaves them all as they are, and an attached item becomes
an ordinary one only when it falls or is picked. The colony's known world
(`core/known.h`) has a picture of the store of its own, copied tile by tile
with the rest (`crowns_copy_tile()`): settlers know what hung in a crown when
they last saw it, plan their picking on that, and see what is really there
once they come to it.

- **Fruit**: in its season a fruit tree sets its fruit in its crown, a few a
  day, most in the outer blocks (the farther of two picked from the trunk),
  up to its yield. Each fruit falls with a chance per hour, 0.5 % × (1 +
  the days it has hung / 5) *(tuning)*, four times as often in a storm; what
  is left at the end of the season falls. Once fallen it is an ordinary item
  on the ground (§5). A full block lets what comes next fall at once.
- **Dead wood**: a branch that dies hangs in the crown for days before it
  falls (twigs too): 2 % a hour, ten times as much in a storm *(tuning)*.
  Shedding (`tree_shed()`) puts it there instead of on the ground. The
  crowns are gone through once an in-game hour, a slice each tick as the
  plants are.
- **Felling**: everything in the crown falls with it, as what lies on dense
  foliage does today.

### 7.2 Picking

Gathering zones pick fruit in the crowns, under the orders of each fruit
(apples, pears, chestnuts, hazelnuts):

- **From the ground**: a settler standing on a tile picks what hangs in the
  block right above it (one level up, within arm's reach), whatever its
  density.
- **Climbing**: a zone has a *Climb trees* box (off by default). With it, a
  gatherer climbs the trunk onto dense foliage and picks from the block it
  stands on, the eight blocks around it on its level and the one above.
  Climbing takes time (the trunk's climbing cost); a fall comes later (§10).

A block follows the orders of the zone covering it, else of the zone
covering the ground under it (`gather_crown_zone()`): a zone drawn on the
ground picks in the crowns over it. Picking a block takes what the zone
wants of it at once (a harvest's work per fruit); the fruit keeps its
quality and is let fall to the ground below the block, at the picker's feet
from the ground, tossed down from up the tree.

### 7.3 Seen by the player

Selecting a block of foliage shows its items **one by one**, as for a tile of
ground, attached ones marked *(on the tree)*. Fruit in the crown is drawn on
the foliage, small, as items are on the ground.

## 8. Seen by the player

- **Sprites** per species, drawn by `tools/generate_sprites.py`: the bark
  (oak grey-brown and furrowed, beech smooth grey, chestnut in spiralling
  ridges, ash grey with a diamond pattern, birch white with black marks,
  apple and pear dark and scaly) and the foliage (beech glossy and thick,
  birch small and light, ash airy and pinnate, oak lobed and irregular),
  with their autumn colours (beech copper, birch yellow, oak brown late, ash
  falling green, chestnut yellow-brown, apple and pear yellow and red).
- The plant window names the species (*Oak*, *Young oak* for a sapling as
  today's names go), and shows its fruit in season.
- Items and structures show their species in their name and window (§6.3).

## 9. Steps

Each step is a commit of its own, with its tests and docs.

1. **Hazelnuts as items** (§5, done): nuts fall, rot, are eaten and hauled
   as items, and a new world's become items when the game starts; the
   zones' *Hazelnuts* order is gone until the crowns are picked (step 4).
   Animals eat nut items wherever they lie (a store too); chestnuts and
   fruit join them in step 2.
2. **The species** (§2 to §4, done): seven plant types, a `TreeDef` each,
   crowns, placement, the species layer of the foliage
   (`Map.tree_species`), chestnuts, apples and pears falling to the ground
   (and eaten there by boars and deer), sapling sprites, bark and leaf
   colours by species, turning as the leaves fall. A hazel is felled like
   the other trees (it used to stand through the felling).
3. **The wood species on items** (§6, done): `woods` on items and stacks,
   names, effects, inherited by recipes, kept by structures and roofs (with
   their hit points), chosen by right click in the build menu, the
   blueprint's panel and bills.
4. **Foliage holding items** (§7, done): attached items (a store of their
   own, `core/crown.h`), fruit and dead wood in the crowns, their falls,
   picking from the ground and by climbing (the zones' *Hazelnuts*,
   *Chestnuts*, *Wild apples* and *Wild pears* orders, a *Climb trees*
   box), the one-by-one list of a block, fruit drawn on the foliage.

## 10. Later

- Acorns and beechnuts, with mast years (a heavy crop every few years);
  acorns eaten by settlers after leaching.
- Beating a tree with a long pole to bring nuts and chestnuts down.
- Falling from a tree (a climber may slip: a fall of one or more levels).
- Stockpile and felling filters by species (*fell only the birches*).
- Wooden structures weathering out of doors (the species' weathering is
  recorded, §6.2), weapons whose weight changes the swing, wear of weapons
  in combat.
- Structures and roofs named by their species (*Oak prop*, *Échelle en
  frêne*); workstations keeping the species of their site.
- The counts of the species lists from what the colony has seen (they count
  every branch lying about for now, fog or not).
- Bark and bast: birch bark as tinder that lights even wet, lime bast for
  cords.
- More species (lime, willow, alder, elm, pine, yew for bows), with the
  French elision for those starting with a vowel.
- Leaves by species: the beech keeping its dry leaves in winter, the ash
  coming into leaf late; the lifespan of a birch.
- Other blocks holding items (brambles with their berries, nests...).
- Roasting chestnuts.
