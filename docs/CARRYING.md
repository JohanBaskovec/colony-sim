# Carrying

How settlers carry things with their own body, before they have bags: in a
hand, in both arms, under an arm, on the shoulders, slung across the body.
What an item is like (its weight, room, length and form) decides where it can
go; the settler's strength and arms decide how much. Settlers arrange what
they carry on their own; the player can lock an item on a settler, or in its
hands, and make a settler go and carry an item.

Status: steps 1, 2a and 3 are implemented (§9): the item data, the places
and the arrangement (`core/carry.h`), the capacities of §5, the tools set
down when in the way of a load, the Gear tab by place, the locks and the
Carry and Drop orders. Not yet: actions needing the hands (§6.3), the
parts of §7 listed in step 3. Values marked *(tuning)* are first
guesses. It replaced the old rules (one load within 60 bulk and a weight
from strength, tools carried apart with no place, counting only in the
weight).

## 1. Principles

- **Real things, real bodies.** Each item type has a weight, a room (bulk),
  a length and a form taken from reality. A settler has two hands, two arms
  to tuck things under, shoulders, and later what it wears and carries in.
- **Places, not slots for kinds.** No "weapon slot" or "tool slot": a spear,
  a log and a bundle of reeds go under an arm alike. What goes where follows
  from the item.
- **Everything carried is in a place.** Tools and weapons too: a hand axe
  kept for work takes a hand like anything else.
- **The settler arranges, the player decides what to keep.** Settlers choose
  places on their own and move things between them when needed (a spear from
  the hand to under the arm, to free the hand for work). The player only says
  what a settler keeps on it, or in its hands.
- **The body counts.** A missing or hurt arm takes away the places on its
  side, or what they bear.
- **Deterministic and pure.** Placing is a pure function of what is carried,
  the item definitions and the body (no randomness): unit-tested on its own.

## 2. What an item is like

`ItemDef` keeps `weight` (kilograms) and `bulk` (room, about half a liter a
unit, `ITEM_TILE_BULK`), and gains:

- **`length`**, in centimeters: the longest side. **Long** means
  CARRY_LONG_CM (60 cm) or more *(tuning)*: long items go under an arm and on
  the shoulders.
- **`form`** (`ItemForm`), how it holds together in a grip:

| Form | What | Examples |
|------|------|----------|
| Rigid | One solid piece | cobble, flint, hand axe, log, spear, root |
| Bundle | Many stems or sticks gathered | twigs, grass, nettles, bracken, reeds |
| Loose | Small pieces that run between the fingers | berries, nuts, rose hips |
| Limp | Drapes over what holds it | carcasses, bodies (later hides, furs) |

- **`strap`** (bool): it has a strap or a cord to sling it (a bow, a quiver,
  later bags). None of today's items.

## 3. The places

| Place | Arms needed | What it takes *(tuning)* | Room | Most weight |
|-------|-------------|---------------------------|------|-------------|
| Right hand, left hand | its own | anything but loose items in a fist; loose ones flat on the palm | 1.5 bulk (loose: 0.5), long rigid items: 2 | 25 % of W |
| Both hands | both | an **armful** (rigid, bundle, limp), or **cupped hands** (loose), or one item **dragged** | armful 60 bulk (rigid short pieces: 12, they roll out; long rigid items: 6), cupped 2 bulk | 75 % of W |
| Under the right arm, under the left arm | its own | long rigid items and long bundles | 20 bulk (long rigid items: 2) | 20 % of W |
| Shoulders | none, but see steadying | long rigid items, long bundles, rigid items of 10 kg or more, one limp item | 60 bulk (long rigid items: 3; limp: 1) | 100 % of W |
| Slung | none | strap items | 2 items | 25 % of W |

W is the weight the settler carries without slowing down (§4).

Rules between places *(tuning)*:

- **One item always fits a place it may go to, whatever its room**, if it is
  within the place's weight (a log in one hand, a stone block in both arms),
  but not loose items: they need the room.
- **Both hands** hold one thing at a time: an armful, cupped hands or a
  dragged item. With both hands busy, the right and left hands hold nothing
  else; an **armful also fills the places under the arms** (the arms are
  around it). Cupped hands and dragging leave them free.
- **Steadying.** A load on the shoulders over 25 % of W, or long rigid items
  on the shoulders whatever their weight (they tip), needs **a hand steadying
  it**: that hand holds nothing else. A limp load over 50 % of W (a carcass
  held by its legs) needs both hands.
- **A single item over every place** is still carried, alone: on the
  shoulders, held with both hands, if it is limp and weighs up to 2 W (a body,
  a boar); dragged with both hands otherwise (a red deer). Walking slows down
  as today.

## 4. Limits: strength, arms, total weight

- **W**, the weight carried without slowing down, comes from strength as
  today (40 kg at strength 10, 2 kg more or less per point), times the
  vigor of the body (`health_vigor()`), no longer times the use of the
  hands: the arms count place by place.
- **Each arm** has its efficiency (its body part's, `docs/HEALTH.md`). A hand
  and the place under that arm bear their weight times it. Below
  CARRY_ARM_MIN (20 %) *(tuning)*, or lost, that side holds nothing.
  **Both hands** need both arms, and bear their weight times the weaker one.
  A settler without a right hand has no right hand nor right armpit to carry
  with, no armful nor cupped hands, steadies a shoulder load with the hand
  left, and drags with it. A limp load needing both hands goes on its
  shoulders only up to 50 % of W.
- **Total.** A settler picks up a load only up to W in all (kept items
  included), as today; kept items alone may go over it (then walking slows
  down: W / carried, never below COLONIST_MIN_LOAD_SPEED).

## 5. What a settler carries of each item today

For a settler of strength 10 (W = 40 kg), both arms whole, carrying nothing
else, the best arrangement of a load of one type. "Today" is the current
rule (60 bulk and 40 kg). Values to check against reality *(tuning)*:

| Item | Weight | Length | Form | Today | Then | How |
|------|-------:|-------:|------|------:|-----:|-----|
| Wood (a log) | 7 kg | 70 cm | rigid | 3 | 5 (35 kg) | one under each arm, 3 on the shoulders (a hand steadying) |
| Stone block | 20 kg | 40 cm | rigid | 2 | 1 | on the shoulders |
| Cobble | 1 kg | 10 cm | rigid | 40 | 8 | in the arms |
| Flint | 1 kg | 12 cm | rigid | 40 | 8 | in the arms |
| Branches | 1.5 kg | 150 cm | rigid | 6 | 9 | 2 in a hand, 2 under each arm, 3 on the shoulders (the other hand steadying) |
| Twigs (dry, wet) | 0.2–0.3 kg | 40 cm | bundle | 30 | 30 | in the arms |
| Grass (fresh, dry) | 2.5 / 1 kg | 60 cm | bundle | 2 | 6 | one in each hand, one under each arm, 2 on the shoulders |
| Bracken | 1 kg | 100 cm | bundle | 2 | 6 | the same |
| Reeds | 1.2 kg | 200 cm | bundle | 3 | 7 | the same, 3 on the shoulders |
| Heather | 1.5 kg | 50 cm | bundle | 4 | 4 | in the arms |
| Moss | 0.4 kg | 20 cm | bundle | 3 | 3 | in the arms |
| Nettles, wild garlic, plantain, wood sorrel | 0.2 kg | 15–40 cm | bundle | 40 | 40 | in the arms |
| Blackberries | 0.3 kg | – | loose | 60 | 2 | cupped hands |
| Bilberries | 0.3 kg | – | loose | 120 | 4 | cupped hands |
| Wild strawberries, rose hips, hazelnuts | 0.1–0.2 kg | – | loose | 149 | 5 | cupped hands |
| Chestnuts | 0.15 kg | – | loose | – | 4 | cupped hands |
| Mushrooms | 0.1 kg | 8 cm | rigid | 120 | 24 | in the arms |
| Wild apples, wild pears | 0.2 kg | 6–8 cm | rigid | – | 17 | in the arms |
| Wild carrots, cattail rhizomes, roasted | 0.25 kg | 20–30 cm | rigid | 60 | 12 | in the arms |
| Flake | 0.03 kg | 6 cm | rigid | 299 | 60 | in the arms |
| Chopper | 1 kg | 12 cm | rigid | 30 | 6 | in the arms |
| Hand axe | 0.6 kg | 15 cm | rigid | 40 | 8 | in the arms |
| Digging stick | 1 kg | 100 cm | rigid | 10 | 9 | as branches |
| Throwing spear | 0.8 kg | 180 cm | rigid | 6 | 9 | as branches |
| Thrusting spear | 1.5 kg | 220 cm | rigid | 5 | 9 | as branches |
| Hare | 3.5 kg | 60 cm | limp | 1 | 3 | one in each hand by the legs, one on the shoulders |
| Fox | 6 kg | 100 cm | limp | 1 | 3 | the same |
| Roe deer | 22 kg | 120 cm | limp | 1 | 1 | on the shoulders, both hands holding the legs |
| Wolf | 35 kg | 130 cm | limp | 1 | 1 | on the shoulders, both hands holding the legs |
| Boar | 70 kg | 140 cm | limp | 1 | 1 | on the shoulders, both hands, slowed |
| Settler's body | 65 kg | 170 cm | limp | 1 | 1 | on the shoulders, both hands, slowed |
| Red deer | 120 kg | 200 cm | limp | 1 | 1 | dragged, slowed |

What it changes: **loose food and small stones come back by the handful**
(blackberries 60 → 2 a trip, cobbles 40 → 8) until containers (§9). Long
things and bundles come back by more than today. A tool or a weapon kept
takes a hand or a place under an arm off every trip. Gathering zones,
hauling and the food stock will be slower: the benchmarks' state hash changes,
and the starting food may need retuning.

## 6. How settlers arrange what they carry

### 6.1 Kept items and the load

What a settler carries is either:

- **kept**: locked by the player (§7), or a tool or weapon it keeps for its
  work (§6.4). It stays on the settler between jobs.
- **the load**: what it is bringing somewhere (hauling, ingredients and
  materials, food, a body). It is gone when delivered.

### 6.2 Placing

A pure function places everything carried (tested on its own, as §5):

1. Items locked **in hand** go into a hand: the better arm first (the right
   one when equal), both hands for an item that needs them.
2. Other kept items go where they leave the hands free, by form: long rigid
   items under an arm, then on the shoulders, then in a hand; long bundles
   the same; limp items on the shoulders, then in the arms; strap items
   slung; short rigid ones in a hand (there is nowhere else without a belt or
   a bag).
3. The load takes the arrangement of the places left that carries the most of
   it; at equal count, the one with fewer places used, then the one that
   leaves a hand free. Kept items may move to make room (a spear from a hand
   to under the arm, so that both arms can take an armful).

The arrangement is searched over the subsets of places the load may take
(at most 128), each filled as far as it goes, kept items placed first out of
the subset, then in it: deterministic. It is not stored: what a settler
carries is its load and its tools, and their places are worked out from them
whenever needed (`carry_arrange()`), so moving an item between places takes
no time.

How much of a type a settler can still pick up (`colonist_carry_capacity()`,
used by hauling, fetching and gathering) is the same search, with what it
already carries.

### 6.3 Work needs the hands

Each action needs **both hands** (knapping, building, gathering, cutting
grass, cleaning, lighting a fire, later butchering), **one** (eating) or
**none** (walking, listening to a story). The tool in use is held in a
working hand.

Before acting, what fills the hands needed moves to another place if it fits
(the spear under the arm), else is **set aside**: put down at the settler's
feet, picked up again when the action is over (the load, to deliver it; a
kept item, always before the settler leaves the spot). Drawing or setting
aside never drops what is locked in hand: a settler whose hands are locked
takes only the jobs the hands left allow (§7.1), and an action may then lack
a hand.

In the core *(step 2b)*: on each of its ticks, before walking or acting, the
settler works out the arrangement of its action (`carry_arrange_working()`):
no load, what is held with both hands and kept in hand first, then the tools
in use in hand (the better arm's first), then as many empty hands as the
action needs beside theirs (`CarryArrangement.working`), then the other kept
items around them. What finds no place is set aside. Set aside is a **state
of the settler** (`Colonist.aside`, the load with it), not items put on the
ground: an action lasts seconds at one spot, the items stay the settler's
(nobody else can take them, nothing to reserve), and the state is over on
the first tick no action needs the hands, before the settler walks off. The
Gear tab shows the hands busy with the work and a *Set aside* branch.

### 6.4 Tools and weapons

Tools follow the same rules as anything carried: a hand axe kept takes a
hand. A settler keeps the tool of its work after it (as today) while it has a
free hand for it; when it needs that hand (hauling, another job's tools) and
the tool fits nowhere else, it sets the tool down: at a workstation, on the
station's tile, where the next craft finds it (haulers store it otherwise),
elsewhere where it stands. The hands a job's action keeps empty only set a
tool aside (§6.3): a gatherer does not leave its axe in the bushes. Eating
never sets a tool down for good. The player locks a tool on the settler to
make it keep it anyway.

Weapons are tools for fighting: a spear kept goes under an arm (or on the
shoulders); later a bow slung. Drawing a weapon into the hands takes *(tuning)*:
nothing if in hand, half a second from under an arm or the shoulders, two
seconds slung. A thrusting spear needs both hands, a throwing spear one.

### 6.5 Sleeping, falling, dying

Before sleeping, a settler delivers or sets down its load (as a hauler
finishes what it holds today); kept items stay with it (laid at its side, for
the rules: still carried). A settler that collapses, faints or dies drops
everything on its tile, kept items included; locks stay on the items while
the settler lives, so that it takes them back once up.

## 7. The player

### 7.1 Locks

Each item a settler carries, its tools as its load, has three states, set
with two buttons on its row in the Gear tab (§7.3); a click on the active one
clears it:

- **Free** (the default): the settler may set it down, store it, hand it in
  as an ingredient.
- **Kept on the settler** (a padlock): it always carries it, wherever it fits
  best (§6.2), and moves it between places as it needs; it never stores it,
  uses it up nor sets it down for good. A hunter keeps its bow while it hauls
  and crafts, as long as the bow is not needed.
- **Kept in hand** (a hand): the same, and it stays in a hand, ready (both
  hands for an item that needs both). The settler does only what the hands
  left allow: with a spear in hand, it hauls with the other hand, under the
  arms and on the shoulders, and leaves the work that needs both hands. For an
  enemy in sight, so that it is ready to strike.

Locks are on an item (this very spear), not on a type; a lock on a type ("a
spear, the best there is") may come later. A settler may keep several locked
items of one type (two spears), but only one free one.

The items of the load (what the settler hauls, fetches or brings to a site)
are locked one by one too: the item becomes a kept item, locked, and the
rest of the load goes on where it was going. It is refused, with the reason
in the tooltip, when there is no room for it beside what the settler keeps
locked (eight kept items at most; kept in hand, a free hand).

### 7.2 Making a settler carry an item

**Carry** sends a settler to fetch an item and keeps it on the settler at
once (else it might set it down as soon as it holds it). From the item (its
window: *Carried by...*, a list of settlers) or from the settler (selected, a
right click on an item: *Carry <item>*, as the move orders). It is refused,
with the reason in a tooltip, when the item cannot fit even with everything
free set aside, when the settler cannot reach it, or when another settler
holds it (handing over comes later). It takes the item over from another
settler's reservation (that job is dropped), and a forbidden item may be
carried this way.

A **Drop** button on each row of the Gear tab puts the item down where the
settler stands, clears its lock and **forbids** it, so that the settler does
not pick it up again at once; the player allows it from its window. Once
the load is empty, the settler gives up what it carried it for.

Done *(step 3)*: Carry is an action of the selected settler's window that
arms a tool, and a click on an item gives the order (the right click already
means "step back"); the tool's card says why over an item it would refuse.
The item's window lists the living settlers under *Carried by...*: a click
on one gives it the order; those who cannot are dimmed, the reason in their
tooltip (`colonist_carry_refusal()`: down, the item gone, held by another
settler, no room beside what it keeps locked, out of reach or unexplored).
The settler walks to the item, takes it if it still lies there or next to
where it stops, keeps it locked on itself and sets down the free items that
no longer fit; a free kept item of the same type is set down (it would be a
spare). A refused order leaves the tool armed. Taking an item over from
another settler's reservation is not done yet: the item is taken if it still
lies there.

### 7.3 The Gear tab

The Gear tab of a settler shows what it carries as a **tree**, like the
gathering zone window. At the top: the strength, the weight carried out of W
and the walking speed it leaves. Then a branch per place, in body order:
right hand, left hand (or **both hands** when one load takes them), under
the right arm, under the left arm, shoulders, slung. A branch holding items
can be folded and unfolded (unfolded by default; the choice is kept while
the game runs, for every settler) and shows the weight it holds; under it, a
row per item, the load's as the kept ones (three branches: three rows): its
name and weight, the two lock buttons, Drop and, on a weapon's row, the
primary weapon button (two crossed blades, `docs/COMBAT.md` §7). Quality and condition are
not shown there (the item's window has them). An empty place says so,
dimmed; a lost or useless arm's places are shown as such, and a hand
steadying the shoulders says so. A last branch, *No place*, lists the items
that found none. The tools worn out stay. Containers will be branches of
their own, folded and unfolded the same way (§8).

## 8. Later

- **Self-defense**: attacked, a settler draws its weapon (done,
  `docs/COMBAT.md` §7); what fills the hands the weapon needs should be set
  aside and picked up after the fight (today the load is put down and free
  tools that no longer fit are set down).
- **Containers** (baskets, bags, packs) take a place (a basket in a hand, a
  bag slung, a pack on a new **back** place) and are places themselves, with
  their own room and weight: **sub-places**. The hand holding a basket holds
  nothing else, but the basket takes the berries. The Gear tab nests them
  under their container.
- **Clothes**: a belt (a hand axe at the waist), clothes as places of their
  own. Carrying on the head (a basket). A sledge to drag.
- **Locks on a type** (always the best spear there is); handing items over
  between settlers; loads of several types bound for different places in one
  trip.
- **Showing what is carried** on the settler's sprite.

## 9. Steps

1. **Item data**: `length`, `form` and `strap` in `ItemDef` for every item
   (§2, §5 values); tests that every type has them.
2. **Places in the core**:
   - a. *(done)* placing (§6.2) as a pure function (`core/carry.h`), tested
     against §5 and with lost and hurt arms (`health_arm_efficiency()`);
     capacity from it (`colonist_carry_capacity()`), so that hauling,
     fetching, building and refuelling follow it; the weight carried without
     slowing down from strength and vigor (the arms count place by place);
     tools set down where the settler stands when they fit nowhere, or when
     they are in the way of a load and not those of its job
     (`colonist_make_room()`, §6.4); the Gear tab by place (read only).
     The load and the tools stay stored as before (`Colonist.carried`,
     `Colonist.tools`).
   - b. *(done)* actions needing the hands and setting aside (§6.3:
     `colonist_action_hands()`, `carry_arrange_working()`, `Colonist.aside`);
     a tool kept after its work only while a hand is free for it; setting
     tools down at the workstation's tile (§6.4).
3. *(done)* **Locks and Carry** (§7): kept on the settler, kept in hand
   (placed first, in the hand of the better arm; while any, the settler only
   starts hauling jobs), the Carry and Put down orders
   (`colonist_order_carry()`, `colonist_put_down()`), each item of the load
   locked or dropped (`colonist_keep_load_item()`,
   `colonist_drop_load_item()`, `items_load_take()`), dropped items
   forbidden, locked items never set down (`colonist_make_room()`), several
   locked items of one type; the Gear tab as a tree of the places, folded
   or not, with a row per item carried, each with its lock and Drop
   buttons; the Carry action; the item window's *Carried by...*, the
   reasons of a refused Carry (`colonist_carry_refusal()`), leaving the
   current job at once when the hands get locked.
4. Drafting and wielding weapons are done (`docs/COMBAT.md` §7, §8: a
   weapon wielded is placed first, a two-handed one held with both hands,
   `carry_arrange_gripped()`). Later: containers.
