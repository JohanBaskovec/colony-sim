# Game design

*Living document. Sections marked **(open)** are decisions still to be made.*

## Vision

A top-down colony simulation where a handful of settlers try to build a home in
a hostile, procedurally generated land. The player never controls settlers
directly: they set priorities, designate work and plan the base, and the
settlers act on their own, driven by their needs, skills and moods. Stories
emerge from the interaction of systems rather than from a script.

## Pillars

1. **Emergent stories**: every settler is a person with traits, relationships
   and a mood; systems interact and produce memorable situations.
2. **Readable simulation**: the player can always find out *why* something
   happened (inspectors, logs, tooltips).
3. **Indirect control**: orders are designations and priorities, not unit micro-management.
4. **Fair challenge**: setbacks are part of the game, but the player is given
   signals and the means to react.
5. **Accessible to everyone**: multi-language from day one, remappable
   controls, readable UI, adjustable difficulty.

## Setting and tone **(open)**

To be decided: era and technology level (frontier/pre-industrial, post-collapse,
sci-fi outpost...), tone (survival, cozy, dark humor), visual style, final title.
The engine and early milestones do not depend on it.

## Core loop

```
observe the colony → designate work / plan buildings / set priorities
        ↑                                                   ↓
react to events  ←  settlers work, eat, sleep, socialize  ←─┘
```

Session goals: survive seasons, grow the colony, reach an end goal **(open)**
(e.g. build a specific structure, reach a population, leave the region).

## Systems

### World
- Procedurally generated tile map (done: terrain, rock, vegetation).
- Day/night cycle (done), seasons affecting temperature and plant growth.
- Weather (rain, snow, heat waves, storms), temperature per tile and per room.
- Natural resources: wood (trees), stone and ores (rock walls, mined), wild food.
- Fog of war / exploration **(open)**.

### Settlers
- Needs: food, rest, recreation, comfort, safety, temperature.
- Skills (construction, mining, growing, cooking, crafting, medicine, combat,
  social...) improving with practice.
- Traits that modify behavior and mood.
- Mood: sum of situational modifiers (ate a good meal, slept outside, saw a
  death...). Low mood → reduced work, then mental breaks.
- Relationships between settlers (friendship, rivalry, romance, family).
- Health: body parts, injuries, diseases, treatment.

### Work and AI
- Job system: designations (chop, mine, harvest, build, haul) produce jobs;
  settlers pick jobs by **work priority grid** (per settler × work type) and distance.
- Pathfinding (A* on the tile grid, movement costs from terrain and plants).
- Reservation system so two settlers don't grab the same job/item.
- Schedules (sleep, work, free time).

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
- Real-time with pause; ranged and melee; cover; injuries feed the health system.

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
