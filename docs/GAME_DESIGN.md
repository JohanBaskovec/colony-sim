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

1. **Emergent stories**: every settler is a person with traits, relationships
   and a mood; systems interact and produce memorable situations.
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

### Vision and fog of war
- At the start the whole map is covered by fog of war; it is uncovered by what
  settlers see.
- Settlers have a **field of view**: walls block sight, but a settler standing
  right next to a wall corner can peek around it (instead of seeing only a 45°
  wedge, as plain line-of-sight from the tile center would give).
- Explored areas that no settler currently sees keep being **simulated**, but
  are **displayed as they were last seen**.
- Vision makes exploration valuable and combat more tactical.

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
- Traits that modify behavior and mood.
- Mood: sum of situational modifiers (ate a good meal, slept outside, saw a
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
- Room detection (enclosed areas) with room quality (size, beauty, cleanliness).
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
