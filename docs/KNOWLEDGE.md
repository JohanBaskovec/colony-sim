# Knowledge, items and constructions

*Living document: the reference for what settlers can know, make and build.
Mostly design: steps 1 and 2 of the implementation plan (§12) are done; the
ROADMAP says what is implemented.
Values marked *(tuning)* are first guesses, to adjust in play.*

The project owner keeps a historical outline of the content, from the Upper
Paleolithic to the Iron Age. This file turns it into game rules, a few pieces
at a time as development reaches them.

**Current scope**: the knowledge system, and the seven **starting
knowledges** every first settler has: Fire, Stone knapping, Gathering, Spear
making, Spear hunting, Butchering, Basic shelters. Other knowledges are
detailed when they are developed.

## Contents

1. [Knowledge](#1-knowledge): levels, what they unlock, how they are learned and lost
2. [Shared rules](#2-shared-rules): units, quality, tools, workstations and bills
3. [Start of a game](#3-start-of-a-game)
4. [Raw materials and gathering orders](#4-raw-materials-and-gathering-orders)
5. [Fire](#5-fire)
6. [Stone knapping](#6-stone-knapping)
7. [Gathering](#7-gathering)
8. [Spear making and spear hunting](#8-spear-making-and-spear-hunting)
9. [Butchering](#9-butchering)
10. [Basic shelters](#10-basic-shelters)
11. [Changes to what already exists](#11-changes-to-what-already-exists)
12. [Implementation plan](#12-implementation-plan)
13. [Future content](#13-future-content): noted for later, not detailed
14. [Open questions](#14-open-questions)

---

## 1. Knowledge

### 1.1 Principles

- **Knowledge lives in people.** Each settler has their own level, from 0 to
  100, in each knowledge. There is no colony-wide research and no tree: no
  knowledge requires another one to be learned, so knowledges can be learned
  in any order.
- **Everything is learned, nothing comes for free.** Making a computer is a
  knowledge like lighting a fire, and must be learned the same way. A stone
  age colony does not build computers because nobody has the slightest idea
  of them: nobody can talk about them or teach them, and nothing the colony
  does leads there by related practice (§1.5). On top of that, a recipe needs
  materials, tools and workstations that come from many other knowledges.
- **Below 50, a knowledge unlocks nothing.** A settler with 49 in Stone
  knapping cannot knap at all. From 50, the settler starts to master it:
  recipes and constructions of the knowledge become available **to that
  settler only**, each from its own minimum level (50 or more, §1.3).
- **The level sets how well the work is done**: speed and quality (§1.4).
  Knowledge replaces skills: there are no separate skills any more.
- **Knowledge spreads** by conversation, evening fireside stories, lessons,
  apprenticeship, related practice, and above all by doing (§1.5).
- **Settlers differ by what they know**, not by innate talents: their levels
  come from their life before the colony (§3) and from what they do in it.
- **Knowledge fades** without practice (§1.6), and **is lost** when nobody alive
  has it any more.
- **No eras.** Eras survive only as a way to group knowledges on screen and as
  an input of the event director (what traders sell, how raiders are
  equipped).

### 1.2 Knowledge definition

| Field             | Meaning |
|-------------------|---------|
| `id`              | Stable identifier (saves), never shown: `fire`, `stone_knapping`... |
| `name_key`, `description_key` | Translation keys. |
| `group`           | Display group (historical period), e.g. Upper Paleolithic. |
| `starting`        | Every first settler starts with it (§3). |
| `influences`      | Activities and item uses that raise it as related practice, with a weight (§1.5). |

**Making and using are separate knowledges** when they are different crafts,
practiced by different people: Bow making and Archery, Spear making and Spear
hunting. A trivial tool stays in the knowledge that uses it (the digging stick
is part of Gathering). Some activities need no knowledge at all: hauling,
eating, drinking, sleeping, cleaning, refuelling a fire, wearing clothes.

### 1.3 What a level unlocks

- Every recipe, construction and order belongs to **one knowledge** and has a
  **minimum level**, 50 or more. Hand axe: Stone knapping 70. Chopper: 50.
- A settler can do the work only with at least that level. The work
  priorities grid keeps its work types (gathering, crafting, construction,
  hunting, cooking, hauling...); a settler takes a job of an enabled work type
  only if they have the level for that particular job.
- The player can place a bill or blueprint if **at least one living settler**
  has the level. Otherwise the button is greyed out with a tooltip ("Nobody
  in the colony knows Stone knapping well enough: 70 needed, best is Mara
  with 62").
- A settler who drops below a minimum level (fading, §1.6) can no longer do
  that work. Bills and blueprints nobody can do any more stay, flagged.

### 1.4 Effect of the level on work

The level (L, from 50 to 100) of the job's knowledge replaces the old skill
level (0 to 20), with the same curves:

| Effect | Formula | L = 50 | L = 75 | L = 100 |
|--------|---------|--------|--------|---------|
| Work speed | `0.5 + (L − 50) / 50` | ×0.5 | ×1.0 | ×1.5 |
| Quality score (0 to 100, one grade per 20 points) | `25 + 1.4 × (L − 50) ± 10` | crude to rough | standard | superb |
| Condition of products (% of hit points, max 100) | `50 + 1.6 × (L − 50) ± 10` | about 50% | about 90% | 100% |
| Failure chance (risky recipes only) | per recipe | high | lower | none |

Quality is also capped by the materials (§2.2).

### 1.5 Learning

Every gain is in points of level, computed as:

```
gain = source rate × hours × age factor (§1.8) × difficulty(L)
difficulty(L) = (110 − L) / 60
```

`difficulty` makes the basics quick and mastery slow: ×1.83 at level 0, ×1 at
50, ×0.17 at 100. A settler working full time on the recipes of a knowledge
goes from 50 to 100 in about 110 hours of work (the old
skills took 210) *(tuning)*.

| Source | Rate (points per hour) | Cap | Notes |
|--------|------------------------|-----|-------|
| **Doing** the knowledge's own work | 1.0 | 100 | Fastest. Needs at least 50, and the recipe's own minimum. |
| **Apprenticeship** (assisting someone working) | 1.5 × master level / 100 | master's level | The only fast way below 50. |
| **Lessons**: fireside stories, later classes | 1.0 × teacher level / 100 (classes 2.0) | 60, and the teacher's level | Several listeners at once. |
| **Related practice** (influences) | 0.1 × weight | 100 | How a knowledge nobody has is invented. |
| **Conversation** | 0.3 × speaker level / 100 | 30, and the speaker's level | Passive, happens on its own. |

Nobody learns beyond the level of the person they learn from. Past 30
(conversation) or 60 (lessons), only apprenticeship and practice raise a level.
Each source thus has its own role: you hear about things, you get taught the
basics, you learn the trade at a master's side, you master it by doing it.

**Doing.** Each hour spent on a recipe, construction or order of the knowledge
gives the full rate, whatever the work speed. Failures count too (one learns
from mistakes).

**Apprenticeship.** A settler who lacks the level for a job of an enabled work
type can **assist** a settler who is doing that job (at a workstation, on a
construction or an order). The apprentice works at the master's side and adds
25% of a worker to the work speed *(tuning)*. Apprentices get the rate above,
up to the master's level. Once at the job's minimum level, the apprentice works
alone. Masters take at most one apprentice each. This needs no classroom and
works from the very first day: it is the stone age way to learn a trade.

**Lessons.**
- *Fireside stories* (from the start): in the evening free time, a settler with
  at least 50 in a knowledge marked "teach" by the player tells about it by a
  burning hearth. Settlers at rest within 5 tiles listen (at most 8). One topic
  per evening, chosen among the marked knowledges where the storyteller is
  best.
- *Classes* (later, unlocked by a knowledge such as a scribe school):
  classrooms, a schedule, teachers and students enrolled per class, topics
  chosen by the player. Faster (rate 2.0), same cap of 60. Designed when that
  knowledge is detailed.
- Teaching counts as practice for the teacher (§1.6).

**Related practice.** Each knowledge lists the activities and item uses that
lead to it, with a weight from 0 to 1 (`influences`). Example weights for
knowledges from the historical outline, to confirm when they are detailed:

| Practice | Raises | Weight |
|----------|--------|--------|
| Gathering (cutting grass, picking plants) | Cordage and fibers | 0.5 |
| Stone knapping (flakes, hand axes) | Blade technology | 0.5 |
| Fire (tending, grilling) and stone knapping | Fat lamp | 0.2 each |
| Fire (grilling, roasting) | Advanced cooking | 0.5 |
| Butchering (and handling raw hides) | Skin working | 0.5 |
| Butchering (cracking bones) | Bone and antler working | 0.3 |
| Spear making and stone knapping | Hafting | 0.3 each |
| Spear hunting | Traps | 0.3 |
| Basic shelters (beds) and gathering | Improved bedding | 0.3 |

Related practice has no cap: if nobody knows a knowledge, someone eventually
crosses 50 through practice, and has invented it (a notification says so:
"Mara has worked out how to twist plant fibers into cord").

**Conversation.**
- Two settlers within 3 tiles of each other, both idle, at rest, eating, or
  working side by side, sometimes chat: on average once per in-game hour
  together, for about 6 minutes *(tuning)*.
- Some chats are about a knowledge (40% *tuning*). The speaker picks one of
  their own, weighted by level and by recent practice: settlers talk about
  what they do. Even 1 point is enough to talk about something, but the gain
  is proportional to the speaker's level, so it is tiny.
- Both settlers can learn from each other in the same chat (each on the topic
  the other brings up).
- Chats also feed relationships later (social system).

**Readability.** The settler inspector lists knowledges above 0 by group,
with a bar per knowledge, a mark at 50 and at the minimum levels of its
recipes. A tooltip gives the recent gains by source ("+0.2 Stone knapping
today: chatting with Idris; +1.4: knapping") and the fading (§1.6).

### 1.6 Fading and loss

- A knowledge counts as **practiced** when its own work is done, when it is
  taught, or when it is used as a master for an apprentice.
- After **15 days** (one season) without practice, it loses **1 point per
  15 days**, down to a floor of **80% of the highest level ever reached**
  (`peak`) *(tuning)*. A rusty master stays capable; a skill dropped for years
  fades. Fading below a recipe's minimum makes the recipe unavailable to that
  settler.
- A knowledge is **lost to the colony** when no living settler has at least 50
  in it. It can still come back: from a settler below 50 who gets there by
  practice, from related practice, or from a newcomer who has it. Since nobody
  learns past their teacher's level, a colony whose best is 45 cannot teach
  its way back to 50: only practice can.
- The player gets a warning when only one settler has 50 or more in a
  knowledge ("Only Mara can still knap hand axes").

### 1.7 Discovered knowledges

- The interface shows **only discovered knowledges**: those in which at least
  one living settler has 1 point or more. There is no list of what is still to
  be found: a knowledge appears the day a settler gets a first point in it
  through related practice, or joins the colony with it (notification: "Mara
  is getting an idea: Cordage and fibers").
- When nobody alive has any point in a knowledge any more (the last ones who
  had it died), it disappears from the interface, with a notification and an
  entry in the colony log. It can be discovered again like any other.

### 1.8 Age

| Age | Learning factor |
|-----|-----------------|
| Child (under 14) | ×2 |
| Adult | ×1 |
| Elder (60 and over) | ×0.5 |

Elders learn slowly but know a lot: they are the colony's memory and its best
storytellers. There are no innate talents (§1.1): age is the only personal
factor of learning speed. Babies start with 0 in everything and learn from
their family by conversation, stories and apprenticeship.

### 1.9 Data and determinism

- Per settler and knowledge: level (0 to 100, stored in hundredths of a point),
  peak level, tick of the last practice. About 1 KB per settler for 150
  knowledges.
- Conversations are checked every 250 ticks for pairs of nearby settlers, with
  the game's seeded RNG; all gains are deterministic and covered by the game
  hash.

---

## 2. Shared rules

### 2.1 Units

| Quantity    | Rule |
|-------------|------|
| Time        | 2,500 ticks = 1 in-game hour. Work amounts are in ticks at speed ×1. |
| Work speed  | Knowledge level (§1.4) × tool (§2.3) × tool grade (§2.3). |
| Weight      | Kilograms per item, realistic. |
| Bulk        | Room per item, about half liters with packing gaps; a tile holds 1,000. Stack limit = 1,000 / bulk. |
| Nutrition   | Food need restored per item. 1.0 lasts 36 h, so 1.0 ≈ 3,750 kcal (2,500 kcal a day). Berries: 0.3 kg ≈ 190 kcal = 0.05. |
| Hit points  | Per item in perfect condition; lost through wear, damage and, later, rot. |

### 2.2 Quality of made things

1. The level of the job's knowledge gives a quality score (§1.4).
2. The grade is capped at **the average grade of the materials + 1**: a master
   knapper with a coarse flint makes at best a rough hand axe. Materials
   without quality never cap.
3. Made things come out at 100% hit points, except for failures.

Effects of grades: **tools** work faster (§2.3) and last longer; **weapons** hit
harder; **furniture and buildings** give more comfort and beauty and have more
hit points. Factors from crude to superb: ×0.6, ×0.8, ×1, ×1.2, ×1.5
(durability, damage, hit points), *tuning*.

### 2.3 Tools

- **No equipment slots.** Tools (and spears) are simply carried: a settler
  uses whatever they carry, the best one for each task, without equipping
  anything. Two guards:
  - **at most one tool of each type** per settler (one flake, one
    cobble to knap with...), so that nobody hoards the colony's tools;
  - carried tools **count in the carried weight** (§2.1, strength), so a
    settler loaded with heavy tools hauls less. Small tools weigh next to
    nothing (a flake: 30 g); big ones (axes, picks, spears) are felt.
- **Fetched when needed, never put back after the work.** When a task wants
  a tool type the settler does not carry, they pick the best reachable one up
  on the way (from storage or the ground) and keep it.
- **Put down only** when swapping for a better one of the same type (at the
  place where the better one lies, no extra trip) or when it breaks.
- Job search counts the detour: a settler who already carries the tool is
  preferred over one who would have to fetch it. Tools that someone is
  walking to are reserved.
- Tools carried by a settler are theirs: others never take them.
- Tools wear: **1 hit point per hour of work** *(tuning)*; at 0 the tool breaks
  (notification). A cobble used as a hammer wears too (200 hit points).
- Swapping: before a craft, a settler who carries a tool for it fetches a
  faster one lying within a short detour (about 30 tiles), leaving the old
  one there. Optional tools (cutting grass, building) are used when carried,
  not fetched.
- Tool grade factor on speed: ×0.8, ×0.9, ×1.0, ×1.1, ×1.2, crude to superb.
- Using a tool needs no knowledge of how to make it: anyone can cut with a
  flake. The knowledge that counts is the one of the job.
- **Any item can be a tool** when a recipe or job asks for it: a plain
  cobble is the knapper's hammer (there is no separate hammerstone item).

Speed factor by job and tool (— = impossible with it; "none" = bare hands):

| Job                              | None | Cobble      | Chopper | Flake | Hand axe | Digging stick |
|----------------------------------|------|-------------|---------|-------|----------|---------------|
| Felling a tree                   | —    | —           | —       | —     | —        | —             |
| Mining rock                      | —    | —           | —       | —     | —        | —             |
| Gathering stones, branches, twigs| ×1.0 |             |         |       |          |               |
| Picking berries, nuts, mushrooms | ×1.0 |             |         |       |          |               |
| Cutting grass                    | ×0.6 | —           | ×0.8    | ×1.0  | ×0.9     | —             |
| Digging up tubers                | ×0.3 | —           | —       | —     | ×0.5     | ×1.0          |
| Knapping                         | —    | ×1.0        | —       | —     | —        | —             |
| Woodworking (spears, sticks, torches) | ×0.5 (torches only) | — | ×0.6 | ×1.0 | ×1.0 | — |
| Butchering                       | —    | —           | ×0.6    | ×1.0  | ×0.9     | —             |
| Cracking bones                   | —    | ×1.0        | ×0.8    | —     | ×0.7     | —             |
| Building                         | ×0.8 | ×0.9        | ×1.0    | ×0.9  | ×1.0     | ×0.9          |

Felling needs an **axe** (the first one is the hafted axe) and mining needs a
**pick** (the first one is the antler pick): neither comes from a starting
knowledge. The first wood of a colony is branches and twigs.

### 2.4 Work accidents and dirt

- **Any job can injure** the worker, more or less badly. Each job type has a
  danger rating; the chance of an accident per hour of work falls with the
  level in the job's knowledge (`danger × (1.5 − (L − 50) / 50)`: ×1.5 at 50,
  ×0.5 at 100) *(tuning)*. Injuries need the health system (M6): until then,
  accidents are not rolled.
- Severity is rolled per accident and depends on the job: a small cut while
  knapping, a crushed foot while building, a goring while hunting.

| Danger | Examples | Typical injuries |
|--------|----------|------------------|
| Very low | Picking berries, cutting grass, hauling | Scratch, twisted ankle |
| Low | Gathering branches and twigs, making spears, grilling | Cut, splinter, burn |
| Medium | Knapping, butchering, building | Deep cut, flake in the eye, crushed finger |
| High | Felling trees, mining | Hit by a falling tree or rock, broken bones |
| Very high | Hunting dangerous game | Goring, trampling |

- **Dirt**: some jobs and places make tiles dirty (knapping chips, wood
  shavings, later butchering and cooking). Dirt lowers room quality and, once
  health exists, helps infections spread; it is cleaned by the cleaning job.
  *Dropped:* sharp knapping debris that cuts barefoot walkers (too much
  simulation for what it adds).

### 2.5 Workstations and bills

- A **bill** = a recipe + a repeat mode (*do N times*, *until N in the colony*,
  *forever*) + filters: ingredient search radius (default 30 tiles), minimum
  ingredient grade, allowed workers (level range in the recipe's knowledge).
- A crafter fetches all the ingredients, works at the station for the recipe's
  work amount, and drops the products on the station's output tile; haulers
  take them to storage.
- A recipe has: ingredients, tools needed (worn, not consumed), workstation,
  work (ticks), knowledge and minimum level, products and by-products,
  failure chance if any.
- Unfinished work is kept on the station and resumes, like orders today.
- **Tools of a recipe** are fetched like ingredients, but not consumed: the
  crafter picks the tool up (the best available one), works with it, and
  keeps it afterwards (§2.3). A recipe whose tool does not
  exist in the colony cannot be started (the bill says why).
- **Workstations are built**, not placed: the player places a workstation
  blueprint; a builder brings its materials (fetched like ingredients, by the
  builder until hauling to construction sites exists) and does the
  construction work, with the station's knowledge and level. The general
  blueprint system (§10, step 5 of §12) reuses this.

---

## 3. Start of a game

- The first settlers bring **nothing**: no tools, no food, no materials.
- Each has a level rolled between **50 and 80** in each of the seven starting
  knowledges (`50 + 30 × (sum of two dice 0..5) / 10`) *(tuning)*, and
  **1 to 30** points in three to six other knowledges, picked among those the
  starting knowledges influence (§1.5): they have "heard of" bows, cord or
  tanning. Nobody starts above 50 outside the starting knowledges.
- World generation guarantees ripe berry bushes and drinkable water near the
  start, since the group arrives hungry, and a **flint patch** within reach
  (§4.1): without flint the colony could only knap rough tools.

A typical first day: pick berries; gather stones (cobbles and flint),
branches, twigs and grass; knap flakes and a
hand axe from flint; make spears and digging
sticks; light the first fire by friction; build grass beds and grass and branch walls
near the hearth; go hunting.

---

## 4. Raw materials and gathering orders

### 4.1 Raw materials

| id            | Name          | Category  | kg   | Bulk | Stack | HP  | Quality | Damageable | Source |
|---------------|---------------|-----------|------|------|-------|-----|---------|------------|--------|
| `cobble`      | Cobble        | Resources | 1.0  | 1.5  | 666   | 200 | yes     | no         | Gather stones (§4.2). Today's `stone` item, renamed. |
| `flint`       | Flint nodule  | Resources | 1.0  | 1.5  | 666   | 150 | yes     | no         | Gather stones on a flint patch (§4.2). |
| `branches`    | Branches      | Resources | 1.5  | 10   | 100   | 20  | yes     | yes        | Gather branches. |
| `twigs`       | Twigs (handful) | Resources | 0.2 | 2   | 500   | 10  | no      | yes        | Gather twigs. |
| `fresh_grass` | Fresh grass (armful) | Resources | 2.5 | 25 | 40  | 10  | no      | yes        | Cut grass. Dries into dry grass. |
| `grass`       | Dry grass (armful) | Resources | 1.0 | 25 | 40   | 15  | no      | yes        | Fresh grass left to dry. |
| `wood`        | Wood (log)    | Resources | 7.0  | 20   | 50    | 60  | yes     | yes        | Felling trees (needs an axe: not at the start). |
| `stone_block` | Stone block   | Resources | 20.0 | 20   | 50    | 150 | no      | no         | Mining (needs a pick: not at the start). Was `stone` before step 2. |

Stone types follow what knappers actually need: the stone that is knapped
must be fine grained, even and brittle (it breaks into smooth, sharp flakes:
conchoidal fracture), while the hammer must be **tough** rather than hard, a
rounded stone that takes blows without shattering (a flint hammer would break
quickly). Two types are enough for the start; others are noted in §13, each
with its own use.

- **Cobble**: a rounded stone that fits the hand (quartzite, sandstone,
  granite). The hammer of the knapper, the anvil of the knapping spot,
  a building material. It can be knapped, as the first tools in history
  were, but coarsely: as a knapping material **a cobble counts as crude**
  whatever its grade, so what is knapped from it is at best rough (§2.2). Its
  grade is its toughness, which matters when it is used as a hammer.
- **Flint nodule**: the knapping stone. Its grade is how fine and even its
  grain is, and caps the grade of what is knapped from it (§2.2): a good
  flint source matters.
- The gatherer's Gathering level sets the grade of both: a good eye picks
  good stones.
- **Stones are limited**: each tile of stony ground (not sand) holds 0 to 20
  stones *(tuning)*, cobbles and flint nodules, that gathering takes one by
  one and that never come back: a gathering zone runs out of stones. The
  tile inspector shows what is left in the ground, the zone inspector what
  the zone holds.
- **Flint patches**: flint is only found on some tiles. World generation
  marks them (a noise field, more on gravel and rough stone, rare on rich
  soil): on a flint patch, each stone of a tile is flint with the patch's
  share (up to 60%) *(tuning)*, a cobble otherwise; elsewhere only cobbles.
  Later, flint comes with the geology (chalk and limestone, §13).
- **Branches**: dead or cut branches, the main material of the start: fuel,
  walls, lean-tos, spears, digging sticks, torches. Quality = straightness.
- **Twigs**: kindling to light a fire, quick fuel, filling for branch screens.
- **Fresh grass** dries into **dry grass** while it lies on a tile (on the
  ground or in a stockpile, not carried): **3 days** in mild dry weather,
  faster in sun and heat, not at all in rain or snow outdoors; under a roof it
  dries at the normal rate whatever the weather (M4) *(tuning)*. A stack keeps
  the drying progress of its items (average, like hit points). Fresh grass
  left wet for too long rots (loses hit points). Until weather exists, drying
  runs at the mild-weather rate.
- **Dry grass**: bedding, roof covering, tinder. Fresh grass cannot replace it
  (a bed of fresh grass would mould, wet tinder does not catch).

### 4.2 Gathering orders

Everything the land offers is gathered in **gathering zones**: the player
draws a zone, then ticks the resources to take there (wood, berries,
branches, twigs, grass, flint, cobbles). Each ticked resource is gathered N
times, until N lie in the colony (items lying anywhere count, as for bills),
or forever; wood and berries may be taken from fully grown plants only (on
by default), so that settlers do not walk to young plants that give little.
On a tile that offers several jobs, the plant's stocks come first, then its
fruit, felling it, and stones last. All jobs but felling belong to Gathering
(minimum 50) and produce items whose grade and condition come from the
gatherer's level. **Dig up** is not implemented yet.

| Order            | Target | Work per item | Tool | Yield |
|------------------|--------|---------------|------|-------|
| Stones (zone)    | Soil, rich soil (with or without grass), gravel, rough stone tiles | 200 | none | 1 cobble or flint nodule (flint patches, §4.1) per work cycle; stones never run out, so the zone says how many (a type no longer wanted is not kept: a cobble found while only flint is wanted is left) |
| Branches (zone)  | Tree with branches in stock | 300 | none | The tree's stock: up to 6 branches on a mature tree (scaled by growth); the tree stays, 1 branch grows back every 2 days |
| Twigs (zone)     | Tree or bush with twigs in stock | 150 | none | Up to 10 handfuls on a mature tree, 4 on a bush; 1 grows back per day |
| Grass (zone)     | Grass at growth ≥ 0.5 | 150 | flake best | `growth × 3` armfuls of fresh grass (1 to 3); the grass regrows from 0 |
| Dig up           | Wild tuber plant | 500 | digging stick | 4 tubers (§7) |
| Berries (zone)   | Ripe bush (fully grown with the option), later hazel, mushrooms | see §7 | none | see §7 |

Branch and twig stocks are shown in the tile inspector.

---

## 5. Fire

*Starting knowledge. Heat, light, safety and cooked
food. Fire must be lit, fed and watched.*

### 5.1 Items

| id             | Name          | Category | kg  | Bulk | Stack | HP | Quality | Damageable |
|----------------|---------------|----------|-----|------|-------|----|---------|------------|
| `torch`        | Torch         | Tools    | 0.5 | 3    | 333   | 10 | yes     | yes        |
| `ember_bundle` | Ember bundle  | Tools    | 0.2 | 1    | —     | 100 (burns down) | no | yes |
| `grilled_meat` | Grilled meat  | Food     | 0.4 | 1    | 1,000 | 12 | yes     | yes        |
| `roasted_tubers` | Roasted tubers | Food  | 0.25 | 1   | 1,000 | 15 | yes     | yes        |

- **Torch**: stored unlit; lit at a burning fire and carried,
  it **burns 1 hour** with a light radius of 3 tiles (×0.8 to ×1.2 as long by
  grade). Lights a hearth at once. Later: night work, keeping animals away.
- **Ember bundle**: embers wrapped in dry grass, to carry fire. Never stored:
  made at a burning fire by a fire lighter and carried straight to the hearth
  to light. Goes out after **4 hours**.
- **Grilled meat**: nutrition 0.19. **Roasted tubers**: nutrition 0.07.

### 5.2 Recipes and jobs

| Recipe / job      | Ingredients | Tools | Station | Work | Min level | Products |
|-------------------|-------------|-------|---------|------|-----------|----------|
| Make torch        | 1 branches  | —     | Crafting spot | 125 (0.05 h) | 50 | 1 torch |
| Fire-harden       | 1 hardenable item | — | Burning hearth | 750 (0.3 h) | 55 | 1 hardened item |
| Grill meat        | 4 raw meat  | —     | Burning hearth | 375 (0.15 h) | 50 | 4 grilled meat |
| Roast tubers      | 4 tubers    | —     | Burning hearth | 250 (0.1 h)  | 50 | 4 roasted tubers |
| Light by friction | 1 twigs, 1 grass | — | Unlit hearth | 750 (0.3 h) per attempt | 50 | a fire, with probability `30% + 1.4% × (L − 50)` (30% to 100%) |
| Carry fire        | —           | —     | Burning fire → unlit hearth | 50 | 50 | a fire, always |
| Open hearth       | (construction, §5.3) | | | 250 | 50 | |

- **Fire hardening** dries and chars a wooden point. Hardenable: digging stick,
  throwing spear, thrusting spear. The hardened item is a separate type
  (`<id>_hardened`): max hit points ×1.5, damage ×1.2 for spears; grade and
  condition carry over; only once.
- **Lighting** is a job created when a hearth with fuel is set to burn and is
  unlit. The fire lighter carries fire (ember bundle or lit torch) from a
  burning fire within 60 path cost if there is one, and otherwise makes fire
  by friction. A failed attempt burns its twigs and grass. Impossible in rain
  on an open hearth (weather, M4). A hearth with no fire in reach and no
  tinder cannot be lit: the job says why.
- Grilled food: grade from the cook's level, capped by the ingredients + 1.

### 5.3 Construction: open hearth

| Field       | Value |
|-------------|-------|
| id          | `open_hearth` |
| Footprint   | 1 × 1, not passable |
| Materials   | none (a shallow pit) |
| Work        | 250 ticks (0.1 h), Fire 50 |
| Hit points  | 50 |
| Placement   | Soil, sand, gravel, rough stone; indoors allowed (smoke, M4) |
| Fuel        | Branches (1.5 kg), twigs (0.2 kg), later logs (7 kg). Capacity 20 kg. |
| Burn rate   | 2.5 kg/h: a branch lasts 36 min, a handful of twigs 5 min, a log 2.8 h *(tuning)* |
| Refuelling  | "Keep burning" or "burn now"; haulers refuel below half; fuel filter. |
| Light       | Radius 6 tiles while burning. |
| Heat        | About 15 kW, through the temperature simulation (M4). |
| Fire risk   | Sparks can set fire to grass, beds and branch constructions within 1 tile (M4). |
| Rain        | Heavy rain can put it out, unless under a roof (M4). |
| Workstation | Fire hardening, grilling, roasting, fireside stories (§1.5). |

---

## 6. Stone knapping

*Starting knowledge. The first link of every chain: no flake,
no spear, no butchering.*

### 6.1 Items

| id            | Name             | Category | kg   | Bulk | Stack | HP  | Quality | Damageable |
|---------------|------------------|----------|------|------|-------|-----|---------|------------|
| `chopper`     | Chopper          | Tools    | 1.0  | 2    | 500   | 80  | yes     | yes        |
| `flake`       | Sharp flake      | Tools    | 0.03 | 0.2  | 5,000 | 15  | yes     | yes        |
| `hand_axe`    | Hand axe         | Tools    | 0.6  | 1.5  | 666   | 120 | yes     | yes        |

- **Hammer**: no item of its own: a plain **cobble**, carried as a tool, is
  the knapper's hard hammer, needed by all knapping recipes; it also cracks
  bones. Finer work with a soft hammer (antler, bone) comes later (§13).
- **Chopper**: a stone with a few flakes struck off one edge; the crudest
  cutting tool, easy to make even from a cobble.
- **Sharp flake**: razor sharp, the best cutting tool of the start, worn out
  after 15 hours of work.
- **Hand axe**: shaped on both faces; good at everything except felling trees
  (too light, no handle).

### 6.2 Recipes

| Recipe           | Ingredients | Tools       | Station       | Work         | Min level | Products |
|------------------|-------------|-------------|---------------|--------------|-----------|----------|
| Knap chopper     | 1 cobble or flint | cobble      | Knapping spot | 250 (0.1 h)  | 50 | 1 chopper |
| Strike flakes    | 1 flint or cobble | cobble      | Knapping spot | 375 (0.15 h) | 55 | `3 + (L − 50) / 10` flakes (3 to 8) |
| Shape hand axe   | 1 flint or cobble | cobble      | Knapping spot | 2,500 (1 h)  | 70 | 1 hand axe + 2 flakes |

- The tool of every knapping recipe is a plain cobble, so the chain can
  always start with two stones: one to strike with, one to knap. A cobble
  carried as a hammer is kept like any tool. In the game, recipes are named
  after their product (Chopper, Sharp flakes, Hand axe...).
- Knapping a cobble instead of flint is allowed but caps the product at
  rough (a cobble counts as crude, §4.1). Bills can restrict the stone type;
  by default they prefer flint for flakes and hand axes, and cobbles for
  choppers.
- **Shape hand axe** fails with probability `40% − 2% × (L − 70)` (40% at 70,
  none from 90) *(tuning)*. A failure still gives `1 + (L − 50) / 10` flakes;
  the stone is lost.

### 6.3 Construction: knapping spot

| Field       | Value |
|-------------|-------|
| id          | `knapping_spot` |
| Footprint   | 1 × 1, passable (an anvil stone to sit by) |
| Materials   | 1 cobble (the anvil) |
| Work        | 250 (0.1 h), Stone knapping 50 |
| Hit points  | 150, not flammable |
| Workstation | Knapping recipes |
| Side effect | Chips: dirt on the tile and its neighbours (§2.4) |

---

## 7. Gathering

*Starting knowledge. Wild food, and the plant and mineral
materials of every other knowledge (gathering orders: §4.2).*

### 7.1 Items

| id              | Name          | Category | kg   | Bulk | Stack | HP  | Quality | Damageable | Nutrition |
|-----------------|---------------|----------|------|------|-------|-----|---------|------------|-----------|
| `digging_stick` | Digging stick | Tools    | 1.0  | 6    | 166   | 40  | yes     | yes        | — |
| `berries`       | Berries (existing) | Food | 0.3 | 1   | 1,000 | 10  | yes     | yes        | 0.05 (≈ 190 kcal) |
| `tubers`        | Wild tubers   | Food     | 0.25 | 1    | 1,000 | 30  | yes     | yes        | 0.05 raw (≈ 250 kcal, half digested); roasted: §5 |
| `nuts`          | Hazelnuts     | Food     | 0.1  | 0.4  | 2,500 | 100 | yes     | yes        | 0.075 (≈ 285 kcal) |
| `mushrooms`     | Mushrooms     | Food     | 0.1  | 0.5  | 2,000 | 5   | yes     | yes        | 0.01 (≈ 30 kcal) |

- Hit points stand for shelf life once rot exists (M4): mushrooms and berries
  spoil in days, tubers in weeks, nuts keep for months: the winter food.
- **Mushrooms**: little food but tasty (mood, M4). Below Gathering 65, a picker
  may take a toxic one by mistake: `(65 − L) × 2%` per mushroom *(tuning)*;
  food poisoning once health exists (M6).

### 7.2 Recipes and orders

| Recipe / order     | Ingredients | Tools | Station | Work | Min level | Products |
|--------------------|-------------|-------|---------|------|-----------|----------|
| Make digging stick | 1 branches  | flake or hand axe | Crafting spot | 375 (0.15 h) | 50 | 1 digging stick (hardenable) |
| Harvest berries (existing) | ripe berry bush | — | — | 300 | 50 | 9 berries; the bush regrows |
| Harvest nuts       | ripe hazel  | —     | — | 400 | 50 | 12 nuts; regrows next year |
| Harvest mushrooms  | mushroom patch | —  | — | 100 | 50 | 3 mushrooms; the patch disappears |
| Dig up tubers      | wild tuber plant | digging stick | — | 500 | 50 | 4 tubers; 30% chance the plant regrows *(tuning)* |

### 7.3 New plants

| id            | Grows on | Growth / day | Notes |
|---------------|----------|--------------|-------|
| `hazel`       | Soil and rich soil (fertility ≥ 0.6), forest edges | 0.03 | Shrub (move cost 1.4). Ripe in autumn once seasons exist; until then, when fully grown. Has twigs. |
| `wild_tubers` | Rich soil, preferably within 6 tiles of water | 0.08 | Low plant. Edible from growth 0.8. |
| `mushrooms`   | Soil within 2 tiles of a mature tree | — | Appear at random under trees (more after rain, M4); vanish after 5 days. |

### 7.4 Construction: ground stockpile

The "storage heap on the bare ground" is the existing stockpile zone. It needs
no knowledge. Its filter gains the new categories (tools, weapons, animal
products, corpses).

---

## 8. Spear making and spear hunting

*Two starting knowledges: **Spear making** makes the spears, **Spear
hunting** uses them. Wildlife and the hunting
AI come with M5; the items are fixed now so that storage and butchering are
designed with them.*

### 8.1 Items

| id                | Name            | Category | kg  | Bulk | Stack | HP | Quality | Damageable | Use |
|-------------------|-----------------|----------|-----|------|-------|----|---------|------------|-----|
| `wooden_spear`    | Throwing spear  | Weapons  | 0.8 | 10   | 100   | 50 | yes     | yes        | Thrown (range 6 tiles) or thrust; damage 12. Hardenable. |
| `thrusting_spear` | Thrusting spear | Weapons  | 1.5 | 12   | 83    | 70 | yes     | yes        | Melee only, reach 2 tiles; damage 18; braced against charges. Hardenable. |

In French: *lance de jet* (throwing spear) and *épieu* (thrusting spear).
Both are all wood, hardened in the fire; stone-tipped spears need the
Hafting knowledge (§13).

Damage values are relative, for the M6 health system; grade multiplies damage
(§2.2), hardening adds ×1.2.

### 8.2 Spear making recipes

| Recipe               | Ingredients | Tools | Station | Work | Min level | Products |
|----------------------|-------------|-------|---------|------|-----------|----------|
| Make throwing spear  | 1 branches (grade standard or better: straight) | flake or hand axe | Crafting spot | 3,750 (1.5 h) | 50 | 1 throwing spear |
| Make thrusting spear | 2 branches (standard or better) | hand axe, flake | Crafting spot | 6,250 (2.5 h) | 60 | 1 thrusting spear |

Construction **crafting spot**: 1 × 1, passable, no materials, so no
construction work either: it is ready as soon as it is placed; needs no
knowledge (a place to sit and work). Workstation for torches, digging
sticks and spears.

### 8.3 Spear hunting

| Job | Min level | Notes |
|-----|-----------|-------|
| Hunt small and fleeing game (hare, deer, horse) | 50 | Needs a carried spear. |
| Hunt dangerous game (boar, aurochs) | 65 | Settlers below it do not take these targets. |

- The player designates animals; hunters approach (stealth from their level),
  throw from up to 6 tiles (hit chance from level and distance) or thrust at
  melee range, and pick the spear up after a throw.
- Wounded animals flee and bleed (tracking); dangerous ones fight back (M6).
  A kill becomes a carcass item (§9).

---

## 9. Butchering

*Starting knowledge. Turns carcasses into meat, fat, hides,
bones and sinew: the materials of the next knowledges (skin working, bone
working, sewing).*

### 9.1 Items

| id                  | Name             | Category        | kg   | Bulk | Stack | HP  | Quality | Damageable | Notes |
|---------------------|------------------|-----------------|------|------|-------|-----|---------|------------|-------|
| `carcass_<species>` | (Species) carcass | Corpses        | per species | 2.5 per kg | 1+ | 100 | no | yes | Rots (M4). |
| `raw_meat`          | Raw meat         | Food            | 0.5  | 1    | 1,000 | 10  | yes     | yes        | Nutrition 0.15 (≈ 700 kcal, less digested raw); "ate raw meat" mood penalty; disease risk (M6). |
| `fat`               | Fat              | Animal products | 0.25 | 0.6  | 1,666 | 40  | yes     | yes        | Edible: 0.6 (≈ 2,250 kcal). Later lamps, pemmican. |
| `marrow`            | Bone marrow      | Food            | 0.1  | 0.3  | 3,333 | 8   | yes     | yes        | Nutrition 0.2 (≈ 750 kcal). |
| `raw_hide_small` / `_medium` / `_large` | Raw hide | Animal products | 1 / 8 / 30 | 4 / 20 / 60 | 250 / 50 / 16 | 30 | yes | yes | Rots in about 5 days; useless until Skin working. |
| `bone`              | Bone             | Animal products | 1.0  | 3    | 333   | 200 | no      | yes        | Cracked for marrow; later tools, fuel. |
| `sinew`             | Sinew            | Animal products | 0.1  | 0.3  | 3,333 | 30  | yes     | yes        | Kept for cordage and sewing. |

- **Raw hides** are a waste product with the starting knowledges on purpose:
  they rot, and handling them is related practice towards Skin working (§1.5).
- **Carcasses** are items: the existing hauling (dragging when too heavy)
  moves them. A huge one (aurochs, 600 kg) is better butchered where it lies
  (×0.7 speed, no station).

### 9.2 Recipes

| Recipe         | Ingredients | Tools | Station | Work | Min level | Products |
|----------------|-------------|-------|---------|------|-----------|----------|
| Butcher carcass | 1 carcass  | flake, hand axe or chopper | Butchering spot (or in the field) | 40 per kg of carcass (red deer: 4,400 ≈ 1.8 h) | 50 | §9.3 |
| Crack bones    | 2 bones     | cobble      | Butchering spot | 250 (0.1 h) | 50 | 1 marrow |

### 9.3 Yields

Shares of the carcass weight, times the butcher's efficiency
`0.7 + 0.006 × (L − 50)` (70% at 50, 100% at 100). Grades come from the
butcher's level; the carcass's condition scales the products' hit points.

| Product | Share | Red deer (110 kg) at 100% |
|---------|-------|---------------------------|
| Meat    | 45%   | 99 portions (49.5 kg) |
| Fat     | 5% (more in autumn, less in late winter) | 22 portions (5.5 kg) |
| Hide    | 7%, one hide | 1 medium raw hide |
| Bones   | 15%   | 16 bones |
| Sinew   | 0.5%  | 5 sinew |

First wildlife (M5), for scale:

| Species    | Carcass (kg) | Bulk | Hide | Behavior |
|------------|--------------|------|------|----------|
| Hare       | 3            | 8    | small  | flees |
| Roe deer   | 22           | 55   | medium | flees |
| Red deer   | 110          | 275  | medium | flees; stags fight in autumn |
| Wild boar  | 85           | 213  | medium | fights back |
| Wild horse | 300          | 750  | large  | flees, kicks |
| Aurochs    | 600          | 1,000 (one per tile) | large | charges |

### 9.4 Construction: butchering spot

| Field       | Value |
|-------------|-------|
| id          | `butchering_spot` |
| Footprint   | 2 × 2, passable |
| Materials   | none |
| Work        | 250 (0.1 h), Butchering 50 |
| Workstation | Butchering, cracking bones |
| Side effect | Blood and offal: dirty tiles, smell (mood penalty within 5 tiles, M4), attracts scavengers (M5). |

---

## 10. Basic shelters

*Starting knowledge. Protection from wind, rain and cold
without walls or houses: natural caves and light structures of branches and
grass. These are the first constructions of M3, the stone age versions of
walls, doors, roofs and beds.*

### 10.1 Constructions

| id              | Name                 | Footprint | Materials | Work | Min level | Passable | HP | Flammable |
|-----------------|----------------------|-----------|-----------|------|-----------|----------|----|-----------|
| `grass_bed`     | Grass bed            | 1 × 2 (1 × 1 for now) | 12 dry grass | 625 (0.25 h) | 50 | yes (×1.2) | 20 | yes |
| `grass_wall`    | Grass and branch wall | 1 × 1 per segment, drawn as the edges of a rectangle | 5 branches, 24 dry grass | 5,000 (2 h) | 50 | no | 60 | yes |
| `branch_screen` | Branch screen (door) | 1 × 1, in a wall or cave mouth | 5 branches, 5 twigs | 3,750 (1.5 h) | 55 | yes (×1.5) | 40 | yes |
| `lean_to`       | Lean-to shelter      | 3 × 2, rotated (open side) | 30 branches, 10 grass | 7,500 (3 h) | 60 | yes (back row ×2) | 120 | yes |
| (order) `clear_floor` | Clear cave floor | per tile, rough stone | none | 1,250 (0.5 h) | 50 | — | — | — |

Quality: builder's level capped by materials (§2.2); grass and twigs have no
quality and do not cap.

Material counts are worked out from real structures, for a tile of about
1 m × 1 m (a branch weighs 1.5 kg, an armful of dry grass 1 kg, a handful of
twigs 0.2 kg):
- grass bed: a litter of 2 m × 0.8 m, about 10 cm thick once slept on, dry
  grass at about 80 kg/m³: 0.16 m³, **12 kg**;
- grass and branch wall, a segment 1 m long and 2 m high: 3 posts of 2.4 m
  (40 cm in the ground), 4 cm thick, about 2 kg each, and 8 rails of 1 m,
  2 cm thick, every 25 cm, 0.2 kg each: 7.6 kg, **5 branches**; then 2 m² of
  thatch 15 cm thick tied to the rails: 0.3 m³ at 80 kg/m³, **24 kg of dry
  grass**. About 2 hours: digging the post holes with a stick, tying the
  rails, thatching;
- branch screen, 1 m × 2 m: a frame of 2 uprights and 3 cross bars (3 kg)
  woven with thinner branches (4.5 kg): **5 branches**, the gaps stuffed with
  **5 handfuls of twigs**; about 1.5 hours.
Taking a structure apart takes a quarter of the building work and gives back
half of the materials.

### 10.2 Details

**Grass bed** (the "litter of grass and leaves"): a bed on the ground, owned
by one settler, 1 × 2 tiles (head and foot, turned four ways). *(Ownership
and size implemented: a settler takes the nearest free bed the first night
and keeps it.)*
- Bare ground: rest gain ×0.8, mood "slept on the ground". Grass bed: ×1.0, no
  penalty; comfort by grade.
- Loses 1 hit point a day; below 50%, "slept on old bedding"; at 0, gone.
  Refreshing replaces the grass (4 grass, 125 ticks).
- Hit points: bed 20, wall 60, screen 40 at standard grade, ×0.6 (crude) to
  ×1.5 (superb), like items. The grade comes from the builder's level, at
  most one above the average grade of the graded materials (branches; dry
  grass and twigs have no grade). *(Implemented; comfort and the old bedding
  mood wait for moods.)*

**Grass and branch wall**: posts and rails of branches thatched with dry
grass, 2 m high: the first wall (it replaces the waist-high windbreak first
planned).
- Blocks movement, sight and wind (M4).
- Insulation 0.5 (a wall of logs or stone 1.0): closes a room together with
  a branch screen, a cave mouth too.

**Branch screen**: a frame of branches pushed aside to pass, the first door.
- Passable at ×1.5, blocks wind, insulation 0.3. Counts as a door for room
  detection: a cave closed by grass walls and a screen is a room.

**Lean-to shelter**: long branches leaning on a ridge, covered with branches
and grass, open on one long side.
- A **roof** on its 6 tiles: rain protection, "under a roof" for rot and fire
  (M4), no "slept outside" penalty.
- Back row low (×2 movement cost, a bed fits), front row high enough to stand.
- Does not close a room (insulation 0.4). Facing its open side to a hearth 1
  or 2 tiles away gives warm sleep (M4): the classic camp.
- The player chooses the open side (four rotations).

**Caves**: settling in natural caves and under overhangs.
- A **natural roof** covers floor tiles surrounded by rock: rock within 2 tiles
  in at least 6 of the 8 directions *(tuning, roof detection in M3)*. Like a
  roof, plus a stable temperature (M4).
- **Clear cave floor**: removes loose rock and levels the ground; the tile
  becomes "cleared floor" (move cost 1.0 instead of 1.2, nicer room). No pick
  needed.
- A cave is closed with grass walls and a branch screen, warmed by a hearth
  (smoky without a vent, M4), and furnished with grass beds.

---

## 11. Changes to what already exists

| Today | Planned | Why |
|-------|---------|-----|
| Skills (mining, woodcutting, gathering), levels 0 to 20. | Replaced by knowledge levels (§1). Gathering becomes a knowledge; **Felling** and **Mining** become knowledges of their own, not starting ones. | One system for what settlers know and how well. |
| Trees are felled bare handed. | Felling needs an axe (first: hafted axe); not possible at the start. | Realism; branches and twigs are the first wood. |
| Rock is mined bare handed. | Mining needs a pick (first: antler pick); not possible at the start. | Realism. |
| `stone`: a 20 kg block from mining. | Renamed `stone_block`; `stone` became the 1 kg gathered stone (step 2), now split into `cobble` and `flint` (step 3). | Stone is gathered and knapped from the first day; hammers and knapped stone are different stones. |
| Orders: chop, mine, harvest. | + gather stones, gather branches, gather twigs, cut grass, dig up. | New materials. |
| Items never change by themselves. | Fresh grass dries into dry grass. | Grass drying (§4.1). |
| Item categories: food, resources. | + tools, weapons, animal products, corpses. | Stockpile filters. |
| Sleeping on the ground at full rest gain. | ×0.8 without a bed, and 10% slower work for 12 hours after (a mood later). *(done)* | Gives beds a purpose. |
| Settlers spawn near water. | Also near ripe berry bushes and a flint patch; they bring nothing. | The group arrives hungry and empty handed. |

---

## 12. Implementation plan

Suggested order, each step with its tests:

**M3: Building**
1. *(done)* Knowledge definitions and per-settler levels for the seven
   starting knowledges; skills replaced; work speed, quality and minimum
   levels from knowledge. Learning by doing. Start of a game (§3).
2. *(done)* New materials and orders: stone, branches, twigs, fresh and dry
   grass (drying); gather and cut orders; branch and twig stocks;
   `stone_block` rename; felling and mining locked (no axe or pick yet).
3. *(done)* Workstations and bills, with what they need from tools and construction:
   - stone types: `stone` split into `cobble` and `flint`, flint patches in
     world generation (one guaranteed near the start), shown in the tile
     inspector;
   - workstations built from a blueprint: a builder brings the materials and
     does the work (crafting spot, knapping spot);
   - bills (recipe, repeat mode, filters) and crafting: fetching
     ingredients, working at the station, products on the output tile;
   - carried tools (one per type, counted in the carried weight), fetched
     when a recipe needs one and kept afterwards;
   - knapping and spear making recipes; failures; quality capped by
     materials (a cobble counts as crude).
   Since then: a dirt layer (crafts dirty the station and around it,
   cleaning is a work type; sharp debris was dropped), and bills got a worker level filter, an input search radius
   and a pause, and *until N* counts the stock: stored items and those on
   their way to storage (lying on a workstation, carried by a hauler).
4. Tools: speed factors on orders and recipes, grade factor, wear and
   breaking, swapping for a better tool, the detour in job search.
5. Blueprints and construction: *(done)* construction sites with several
   materials, grass bed, grass and branch wall, branch screen, taking apart,
   sleeping in beds; *(left)* lean-to, floor clearing, workstations as
   construction sites, hauling to construction sites, quality and wear.
6. Roofs (lean-to, natural roof) and room detection.
7. Conversation, apprenticeship, fading and loss; discovered knowledges only
   in the interface; knowledge in the settler inspector.

**M4: Survival**: open hearth, fuel, lighting, torches, ember bundles,
hardening, grilling, roasting, fireside stories; heat, wind, rain, rot;
tubers, hazel, mushrooms with seasons.

**M5: Vision and exploration**: wildlife, spear hunting, carcasses,
butchering, cracking bones.

**M6: Danger**: work accidents and injuries (§2.4).

**Later**: related practice towards the next knowledges once they exist;
classes with schedules and classrooms; children.

---

## 13. Future content

Noted so as not to be forgotten; each is detailed when development reaches
it.

**Hafting** (knowledge). Fixing a stone onto a handle, with cord (knowledge
Cordage and fibers, which gathering leads to) and glue (resin, later birch
tar). Spear making and stone knapping lead to it (§1.5). It makes at once:

- the **stone-tipped spear**: a flint point on a throwing or thrusting spear,
  more damage than fire-hardened wood;
- the **hafted axe**, the first axe, which unlocks felling trees (with the
  Felling knowledge).

**Blade technology** (knowledge). Long regular blades struck from a prepared
core: better spear points, and real knife blades that can be hafted. Stone
knapping leads to it (§1.5).

**Soft hammer**. An antler or bone hammer (from hunting, M5), an optional tool
for shaping hand axes: thinner flakes and more regular edges, so fewer
failures and a better grade.

**Other stone types**, each with a use of its own (a stone type only exists
if it changes a decision):

| Stone | Use | When |
|-------|-----|------|
| Pyrite | Struck against flint, it sparks: lighting a fire without friction, faster and more reliably | M4 (fire) |
| Ochre (red, yellow) | Pigment: painting, protection from sun and insects; helps tanning hides | With tanning or art |
| Obsidian | The finest edge there is, but brittle: better flakes, faster wear; only in volcanic regions | With biomes |
| Sandstone (abrasive) | Grinding stones; polishing stones for polished axes | Neolithic |
| Tough rock (dolerite, jadeite) | Polished axes, which do not chip like flint | Neolithic |
| Heating stones (granite, basalt) | Boiling water with hot stones; flint and wet river stones burst in the fire (an accident) | M4 (cooking) |

**Geology**. A chalk and limestone rock type in world generation: flint
lies on the gravel near it and in river beds, and mining chalk gives flint
nodules. Replaces the noise-only flint patches of step 3.

**Earth building** (knowledge, after Basic shelters). Walls of earth come
with settling down (first villages, pottery, farming, about 10,000 years
ago), later than grass and branches but not much. In historical order:

| Technique | How | First known | Needs |
|-----------|-----|-------------|-------|
| Wattle and daub | The branch frame of the grass wall, woven into a wattle and plastered on both sides with clay, water and fibers (dry grass, dung) | Neolithic (Linear Pottery longhouses, about 5500 BC) | Clay, water, dry grass; digging stick |
| Cob | Lumps of earth and straw stacked by hand into thick walls (40 to 60 cm), no frame | Neolithic | The same, much more earth |
| Mud bricks (adobe) | Bricks shaped in wooden moulds, dried in the sun for weeks | Near East, about 9000 BC (Jericho) | Moulds, a drying area, dry weather |
| Rammed earth | Earth packed into a formwork of planks | About 2000 BC | Planks, so tools to split or saw them |
| Fired bricks | Mud bricks fired in a kiln | About 3000 BC (Indus, Mesopotamia) | A kiln and a lot of fuel |

What the game needs first:
- **Clay** in world generation: deposits along rivers and in wet hollows,
  shown in the tile inspector (like flint patches); dug with a digging stick,
  then a spade.
- **Water** to mix: a container to carry it (skin, watertight basket,
  pottery), or the mixing done at the water's edge.
- **Drying**: a daub wall takes several days to set; drying too fast
  (sun, wind) cracks it *(tuning)*.

Rough material counts, for a segment 1 m long and 2 m high (same tile as
the grass wall): the frame (5 branches) woven with thinner branches (about
5 more), and about 10 cm of daub in all: 0.2 m³ of earth at 1,500 to
1,800 kg/m³, **300 to 350 kg**, plus a few kilograms of dry grass. That is
about 8 trips at the standard carry weight of 40 kg: containers (baskets,
then barrows) become precious.

What it is worth: far better insulation than grass (about 0.8 against 0.5),
hardly burns, lasts decades if a roof keeps the rain off; the price is the
weight to carry and the drying time.

---

## 14. Open questions

None at the moment.

Settled so far: stones are gathered in zones with a target per stone type
(or forever); grass drying is
simulated; dirt and work accidents exist (no sharp knapping debris); the interface
shows only discovered knowledges; no innate aptitudes; felling and mining are
the knowledges **Felling** and **Mining**; the hafted axe comes from Hafting
(§13), the knowledge that makes the antler pick is decided when it is
detailed; two stone types at the start (cobble and flint); workstations are
built by a builder from the start of step 3, not placed instantly; no
equipment slots: tools are carried, one per type, counted in the weight.
