# Game design

*Living document. Sections marked **(open)** are decisions still to be made.
Ideas marked **(long term)** are directions, not commitments: they may change.*

## Vision

A top-down colony simulation where a handful of settlers try to build a home in
a hostile, procedurally generated land, and lead their colony from the stone age
towards the future. The player never controls settlers directly: they set
priorities, designate work and plan the base, and the settlers act on their
own, driven by their needs, knowledge and moods. Stories emerge from the
interaction of systems rather than from a script.

## Pillars

1. **Emergent stories**: every settler is a person with attributes, tastes,
   relationships and a mood; systems interact and produce memorable situations.
2. **Grounded simulation**: everyday life is simulated believably (thirst,
   bodily needs, warmth, fire, hygiene). Like in real life, it takes effort;
   organizing that effort efficiently is part of the game (see *Logistics*).
3. **Readable simulation**: the player can always find out *why* something
   happened (inspectors, logs, tooltips).
4. **Indirect control**: orders are designations, priorities and roles, not unit micro-management.
5. **Fair challenge**: setbacks are part of the game, but the player is given
   signals and the means to react.
6. **Accessible**: multi-language from day one, remappable controls, readable
   UI, adjustable difficulty.

## Setting and tone

- **Eras**: a colony starts in the **stone age** and can progress through later
  eras (bronze age, medieval... up to the future). Development follows the same
  order: the stone age is built and polished first, then the next eras one by one.
- **Tone**: survival, rather serious, with room for some humor.
- **Audience**: adults. The game may contain mature themes (sexuality, bodily
  functions, violence); an 18+ rating is acceptable.
- **Visual style**: 2D pixel art, top-down. Placeholder graphics until then.
  Tiles are **64 × 64 pixels**. Zooming out far below 1:1 blurs pixel art:
  pre-scaled versions of the sprites (mipmaps) will keep the overview readable.
- **Title** **(open)**.

## Core loop

```
observe the colony → designate work / plan buildings / set priorities and roles
        ↑                                                          ↓
react to events  ←  settlers work, eat, drink, sleep, socialize  ←─┘
```

## Knowledge and progression

- There is no colony-wide research and no tech tree. **Each settler has a
  level from 0 to 100 in each knowledge** (fire, stone knapping, gathering,
  bow making, archery...). Below 50 a knowledge unlocks nothing; from 50 that
  settler, and only them, can make the items and constructions it unlocks.
  The level sets work speed and quality: knowledge replaces skills.
- No knowledge requires another, so they can be learned in any order; but
  everything must be learned, even building a computer. Progress follows
  what the colony does (related practice leads to new knowledges) and what
  its recipes need (materials, tools, workstations from other knowledges).
- Settlers learn by doing, as apprentices at a master's side, from fireside
  stories and later classes, by chatting, and by related practice (which is
  how something nobody knows gets invented). Unused knowledge slowly fades;
  a knowledge is lost when nobody alive masters it any more.
- No eras as a game mechanic: historical periods only group content (from the
  Upper Paleolithic up to the Iron Age, then later) and feed the event director.
- Details, items and constructions: [KNOWLEDGE.md](KNOWLEDGE.md), starting with
  the seven starting knowledges.

## Endings **(long term)**

Several possible victories, in the spirit of strategy games: scientific,
diplomatic, religious...

- **Inner peace** (religious ending): a religion system where each settler can
  unlock levels of spirituality; when at least three settlers reach the highest
  level, the game is won.
- Other endings to be designed.

## Systems

### World
- Procedurally generated tile map (done: terrain, rock, vegetation).
- Day/night cycle (done), seasons affecting temperature and plant growth.
- Weather: rain, snow, heat waves, storms.
- **Temperature, simulated realistically** per tile: heat diffuses through the
  air, walls insulate, and opening a door lets warmth escape quickly. Sitting
  next to a fire makes settlers warm. Caves keep a stable temperature.
- Natural resources: wood (trees), stone and ores (rock walls, mined), wild
  food, water (lakes, rivers).

### Living world **(long term)**

The land is alive on its own, whether the settlers are there or not. Target
map: 256 × 256 tiles, small enough to simulate every tile in depth.

- **Levels** (priority): several levels below and above the ground, 2 m
  each (a tile is 1 m × 1 m, a wall 2 m high: one level is one storey).
  Natural relief (hills, cliffs, caves; no mountains, they would take too
  many levels), dug mines and cellars, buildings with storeys.
  - View: the current level is sharp, the levels below show through open
    space however deep, blurred and a little darker with each level (never
    black).
  - Going up and down: natural slopes on foot; dug ramps, ladders and stairs
    made by the settlers; cliffs cannot be crossed.
  - Support: nothing overhangs more than 2 tiles from what holds it up, the
    rule roofs already follow (`ROOF_SUPPORT_RANGE`): mine galleries need
    props or pillars, storeys need bearing walls, and what is not held
    collapses.
  - Water flows between levels: flooded mines, waterfalls, wells filling up.
  - Trees span several levels: a trunk that can be climbed, foliage in the
    levels above (shade, shelter from rain, gathering nuts and fruit, a
    lookout, a refuge from animals that cannot climb; tree houses later).
    Seen from the ground level, the foliage above is drawn lightly
    see-through, with a key to hide it.
  - Felling: a tree falls in one piece, full length, on the side the
    woodcutter chooses, away from buildings as a real woodcutter would aim
    it. The fall is always exactly where intended, whatever the woodcutter's
    level (a missed fall would be unfair to the player). With no clear side,
    the tree falls where it does the least harm, after a warning on the
    order. The fallen trunk lies across the tiles until it is limbed and cut
    into logs.
  - Number of levels **(open)**: around 32 (64 m: mines below the plains,
    hills, then room for trees and buildings on the hilltops), to be tuned
    with world generation.
- **Soil**: moisture and fertility simulated per tile (rain, shade, nearby
  water, what grows and rots there); light computed from the sun, the canopy
  and roofs.
- **Vegetation** spreads and reproduces believably: seeds carried by the
  wind and by animals, roots and runners; plants compete for light, water
  and fertile soil.
- **Wildlife**: many animals living their own lives: they eat plants, hunt,
  fight, get hurt, breed and die of old age. Large animals are individuals;
  small fauna (mice, insects, birds) may be a density per area **(open)**.
- **Health**: every body part can be hurt, bleed, get infected, be cut off or
  torn away, for settlers and animals alike.
- **Genetics**, semi-realistic (no real DNA): every animal, human and plant
  has traits passed on to its offspring, with random mutations. The world
  drifts on its own, and the player can use it: selecting the best plants
  and animals, and even settlers. Plants carry a variety (a shared set of
  genes) rather than a genome each.
- **Generations**: an aging speed chosen per game (up to ×10, the same for
  every living being; plants follow the seasons), so that
  families grow within a game: children become adults, elders teach the
  young, and knowledge has to be handed down before its masters die.

Performance rules that keep this at 60 frames per second: each system runs at
its own pace (every tick only for what moves; hourly for soil; daily for
plants; at birth for genetics); long work is spread over several ticks
(decisions, path searches with a budget per tick); only what the player can
see or use is simulated as individuals.

### Vision and fog of war
- At the start the whole map is covered by fog of war; it is uncovered by what
  settlers see.
- Settlers have a **field of view**: walls block sight, but a settler standing
  right next to a wall corner can peek around it (instead of seeing only a 45°
  wedge, as plain line-of-sight from the tile center would give).
- Explored areas that no settler currently sees keep being **simulated**, but
  are **displayed as they were last seen**.
- Vision makes exploration valuable and combat more tactical.
- Sight is three-dimensional with no range limit (only floors, walls, hills
  and trunks stop it; a tower sees further), the fog is a real constraint (settlers only act on what the
  colony has seen), and the game starts with what is in view plus the way the
  settlers came from a map edge: `docs/VISION.md`.

### Settlers
- **Needs**:
  - food, rest and water (done: settlers drink at lake shores);
  - water, later: rivers, wells, buckets of water;
  - bladder and bowels: relieve oneself anywhere at first, latrines later;
    poor hygiene (e.g. near a water source) spreads disease;
  - sexual needs: masturbation or sex between settlers, represented
    abstractly; **(long term)** pregnancy and children (growth, aging);
  - recreation, comfort, safety, temperature.
- Names: proper nouns from a built-in multicultural list, identical in every
  language (later: per-culture lists in data files).
- Food, for now: settlers eat berries, from berry bushes (which regrow) or
  harvested onto the ground. Meals, spoilage and farming come later.
- Knowledge levels (fire, stone knapping, gathering, building, medicine,
  combat...) improving with practice and teaching: see *Knowledge and
  progression*.
- Attributes instead of all-or-nothing traits (`docs/ATTRIBUTES.md`): body
  (walking, work pace, learning, sleep, appetite, cold and heat tolerance,
  toughness, pain), mind (outlook, temperament, sociability, demands, courage)
  and social self (kindness, social ease, self-confidence, beauty, sexuality),
  each on a continuous scale where most settlers are ordinary and extremes are
  very rare; shown in words, never in numbers.
- Tastes, later: liking or disliking drugs, foods, animals, women or men,
  kinds of work...
- Mood: sum of situational modifiers (ate a good meal, slept in the rain, saw a
  death...). Low mood → reduced work, then mental breaks.
- Relationships between settlers (friendship, rivalry, romance, family).
- Health: body parts, injuries, diseases, treatment; consequences of hunger,
  thirst and exhaustion.
- **(long term)** Spirituality levels (see *Endings*).

### Work and AI
- Job system: designations (chop, mine, harvest, build, haul) produce jobs;
  settlers pick jobs by **work priority grid** (per settler × work type) and distance.
- Pathfinding (done: A* on the tile grid, movement costs from terrain and plants).
- Reservation system so two settlers don't grab the same job or item.
- Schedules (sleep, work, free time).

### Logistics
Satisfying needs takes time; the player's challenge is to organize it well,
a mini-game in itself:
- assign **roles**, e.g. a water carrier who fills buckets placed near the
  dwellings, so that others drink nearby instead of walking to the lake;
- place storage, water, food and latrines smartly to shorten trips;
- containers (buckets, baskets) and hauling jobs.
- **Weight and bulk**: every item has a realistic weight (kg) and a bulk (room
  taken). A tile holds a fixed amount of bulk, so small things stack high
  (1,000 portions of berries) and big ones do not (50 logs). Settlers carry a
  limited bulk and weight by hand; heavier loads are dragged, slowly (later:
  huge items such as an elephant carcass, equipment worn by hand). Containers
  (baskets, sledges, carts) and strength will raise these limits.

### Base building
- Blueprints → construction jobs consuming materials.
- Walls, doors, floors, roofs; furniture (beds, tables, workbenches, storage).
- Zones: stockpiles (with filters), growing zones, activity areas.

### Production
- Farming (sow, grow according to fertility/season/light, harvest).
- Crafting at workbenches with bills (e.g. "cook until there are 10 meals").
- Food chain: raw food → meals; spoilage and storage temperature.

### Events
- An **event director** paces incidents according to colony wealth, recent
  history and the chosen difficulty: wanderers joining, traders, animal
  attacks, disease, weather events, raids.
- Several director personalities (steady, chaotic, peaceful) **(open)**.

### Combat **(scope open)**
- Real-time with pause; ranged and melee; cover and line of sight; injuries
  feed the health system.

### Persistence and moddability
- Save/load (versioned binary or text format), autosave.
- Game data (terrain, plants, items, recipes) moved to data files so that mods
  can add content and translations.

## Out of scope (for now)

- Multiplayer.
- 3D or isometric graphics.
- A world map with several colonies.

## Reference numbers (current implementation)

| Quantity                 | Value                            |
|--------------------------|----------------------------------|
| Ticks per second (×1)    | 60                               |
| Speeds                   | ×1, ×3, ×6                       |
| Ticks per in-game hour   | 2,500 (≈ 42 s real time at ×1)   |
| Day / season / year      | 24 h / 15 days / 4 seasons       |
| Default map size         | 200 × 200 tiles                  |
