# Roadmap

Milestones are ordered; each one ends with a playable build and passing tests.
Tick items as they land and add newly discovered tasks.

## M0: Foundations ✅

- [x] CMake project, presets, SDL3/SDL3_ttf fetched automatically
- [x] Core/frontend split (core has no SDL dependency)
- [x] Unit test framework + CTest integration, sanitizers, CI workflow
- [x] Logging, PCG32 RNG, value noise
- [x] i18n: catalogs, fallback, plurals, positional placeholders, number formatting
- [x] Translation checks (completeness, placeholders, keys used in code)
- [x] English and French translations, live language switching, system locale detection
- [x] Fixed-timestep game clock, speeds, pause, calendar, day/night
- [x] Procedural map: terrain, rock walls, grass/bushes/trees
- [x] Plant growth by fertility
- [x] Camera (pan, zoom at cursor), tile inspector, HUD, debug overlay

## M1: Colonists ✅

- [x] Entity pools with generational handles
- [x] Colonist entity: name, age, position; starting group spawned on a large reachable land area
- [x] A* pathfinding with movement costs, no corner cutting, nearest-match search (+ tests, + benchmark)
- [x] Movement along paths, interpolated rendering
- [x] Selection (click, Shift+click, box select), colonist inspector panel, path display
- [x] Needs: food and rest; simple decision making (eat berries, sleep in place / at night, wander)
- [x] Names: built-in multicultural list, not translated (proper nouns)

## M2: Work (next)

- [x] Thirst: drink from shallow water or a lake shore; eat and drink before going to sleep
- [x] Designations: chop trees, mine rock (tools, rectangle drag, cancel); idle colonists do the work, one worker per tile (reservations), interrupted work resumes
- [x] Job system: generic reservation table (jobs, food: two colonists no longer walk to the same bush), work types (mining, woodcutting) and per-colonist work priorities, jobs picked by priority then distance
- [x] Items on the ground (wood, stone), one stack per tile with stack limits; chopping and mining drop them, spreading to nearby reachable tiles
- [x] Harvest designation (gathering work): berries as a food item, the bush grows back; colonists eat food items from the ground as well as ripe bushes
- [x] Hauling and stockpile zones: zone tool (rectangle drag, extend, Shift to remove), per-zone priority from 1 (highest) to 10, allowed items by category (food, resources); colonists carry items to the best zone with room, and move stored items to better zones
- [x] Minimal immediate-mode UI (panels, buttons, check boxes) for the stockpile inspector
- [x] Item weight (kg, realistic) and bulk: stack limit per tile from bulk (1,000 berries, 50 logs, 50 stones); colonists carry a limited bulk and weight per trip, and walk slower when dragging more than their weight limit
- [x] Work priorities grid UI: colonists × work types, left click raises a priority, right click lowers it (1 to 4, empty = never); changes apply at once (a disabled work type is dropped, a newly enabled one is picked up without waiting)
- [x] Item quality and hit points: five quality grades (crude to superb) and hit points per item; only items of the same grade stack, a stack keeps the average hit points; raw stone is always superb and cannot be damaged
- [x] Skills (mining, woodcutting, gathering, levels 0 to 20, rolled at creation): the worker's skill sets the grade and condition of what it produces (novices bring back rough, damaged logs)
- [x] Stockpile filters on item condition: quality and hit point range sliders; items a narrowed filter no longer accepts are moved out
- [x] Skill learning: work on an order gives experience in its skill, each level takes longer (one in-game hour of work for level 1, twenty for level 20); progress shown in the colonist inspector, levels in the work priorities grid
- [x] Skills also change work speed: ×0.5 at level 0, ×1 at level 10, ×1.5 at level 20 (shown in the colonist inspector)
- [x] Haul more than one stack per trip: after a pickup, a hauler with room left collects nearby matching stacks bound for the same storage tile (one type and grade per load)
- [x] Haul towards construction sites (M3)
- [ ] Loads of mixed items once containers exist
- [x] Strength (1 to 20, rolled at creation, lower past 40): sets the weight a colonist carries without slowing down (22 to 60 kg, 40 kg at 10); shown in the colonist inspector
- [ ] Carrying capacity also from health (M6); containers (baskets, sledges) and worn equipment adding bulk and weight, once they can be made

## M3: Building

- [x] Design of the knowledge system and of the seven starting knowledges, their items and constructions (`docs/KNOWLEDGE.md`)
- [x] Knowledge definitions and per-settler levels (seven starting knowledges, 50 to 80 at the start), replacing skills: speed, quality and minimum levels from knowledge; learning by doing; start with no tools nor food, ripe berry bushes guaranteed near the arrival point; knowledge levels in the colonist inspector and the work grid
- [x] New materials and orders: gather stones (repeats until cancelled), gather branches, gather twigs (plant stocks that grow back), cut grass; fresh grass dries on the ground; mined stone renamed stone block; felling and mining locked until axes, picks and their knowledges exist
- [x] Workstations and bills: stone split into cobble and flint, flint patches (one near the start); workstations built from a blueprint by a builder who brings the materials (knapping spot), or ready at once when made of nothing (crafting spot); bills (do N times, until N lie in the colony, forever; allowed stone types) and crafting; carried tools (one per type, no equipment slot), fetched when a recipe needs one and kept; knapping and spear making recipes, hand axe failures, quality capped by materials; build tool, workstation inspector (recipes named after their product, with their ingredients and tools), flint and carried tools in the inspectors
- [x] Stone gathering zones instead of a repeating order: gather until the colony has a target of cobbles and of flint (or forever); stones no longer wanted are left; a background job, after construction and crafting at the same priority
- [x] One configurable gathering zone replaces the chop, harvest, branches, twigs, grass and stones orders: a check box per resource (wood, berries, branches, twigs, grass, flint, cobbles), each gathered N times, until N lie in the colony, or forever; wood and berries from fully grown plants only (option, on by default); plant stocks taken before fruit and felling on the same tile
- [x] Dirt: a dirt level per tile (0 to 100 %), crafts dirty the station's tile and its neighbours (chips, shavings), cleaning is a work type (last column, lowest priority by default), dark spots on dirty tiles and the level in the tile inspector; sharp knapping debris dropped (too costly for what it adds)
- [x] Priorities follow the columns of the work grid: at the same number, the leftmost work type is done first (columns reordered: mining, construction, crafting, woodcutting, gathering, hauling, cleaning)
- [x] Bills: worker level filter (minimum/maximum level in the recipe's knowledge, −/+ by 5, Shift by 25; the inspector warns when nobody fits)
- [x] Bills: ingredient search radius (travel cost from the station, 10 to 250, −/+ by 10, Shift by 50), pausing a bill (a craft under way is finished); "until N in stock" counts stored items and those on their way to storage (lying on a workstation, carried by a hauler), refreshed each tick only while such a bill exists
- [x] Workstations and blueprints on plants: refused on trees (felling needs an axe); on grass and bushes the builder clears the plant first (cut grass, twigs, branches and ripe berries dropped, progress kept if interrupted), a crafting spot on a plant is a blueprint until then; the inspectors show what is to clear
- [x] Changing a work priority forgets the failed searches of every kind of work (stations, gathering zones, construction sites, cleaning were waiting for their backoff)
- [x] Zone editing: a rectangle over zones of the same kind merges them (the selected one, else the oldest, keeps its settings); removal tools in the build menu take tiles out of zones (Shift+drag still works)
- [x] Zones of different kinds never overlap: a gathering zone drawn over a stockpile (or the other way round) leaves its tiles out
- [x] Gathering zones: no zone (gathering or stockpile) under a workstation: zones drawn over stations leave their tiles out, a station placed on a zone takes its tile out of it; a zone priority (1 to 10, before distance, after work priority and column); the tile inspector shows the job under way on a zone tile (who, what, how far) or the work left off
- [x] Limited stones: every stony tile holds 0 to 20 stones (none on sand), cobbles and flint (flint patches give a share of flint), taken one by one; the zone inspector shows how many of each resource the zone holds (and warns when an enabled one has none left), the tile inspector the stones left in the ground
- [x] Tools: speed factors by kind of work and tool (bare hands included: cutting grass ×0.6, building ×0.8), grade factor (×0.8 to ×1.2), applied to gathering, clearing, building and crafts; wear (1 hit point per hour of work, cobbles now damageable) and breaking (the craft waits for a new tool; count in the colonist inspector); swapping for a faster tool within a short detour before a craft (the old one is left there)
- [ ] Tools, later: the detour in job search (prefer the settler who already carries the tool: needs jobs assigned among settlers), fetching optional tools (a flake to cut grass, a chopper to build), picking the best reachable tool rather than the nearest, a notification when a tool breaks
- [ ] Starting settlers get 1 to 30 points in a few related knowledges (once non-starting knowledges exist)
- [x] Construction sites: blueprints of structures with several materials, builders fetching each material and working from a neighboring tile, cancelling gives the materials back, planning over a site in progress replaces it (its materials dropped), a *take apart* order (half the materials back)
- [x] First structures, with realistic material counts (dry grass only): grass bed (12 dry grass), grass and branch wall 2 m high (5 branches, 24 dry grass, drawn as the edges of a rectangle), branch screen as a door (5 branches, 5 twigs, ×1.5 movement cost)
- [x] Beds: settlers sleep in the nearest free bed; on the ground rest comes back at ×0.8 and they work 10% slower for 12 hours after
- [x] Zones over several levels: a stockpile or gathering zone drawn (or removed with Shift) from a level covers what is seen in the rectangle, the ground of the levels below where the level shown is open space (`map_view_level()`); zones are drawn in the blurred view of the levels below
- [x] Selection over several levels: a click or a selection box picks the settlers of the level shown and those of the levels below seen through open space (`game_colonist_at()`, `game_colonists_in_rect()`); a drag keeps its first corner in the world, so moving the camera under a held button stretches the rectangle (or box) towards the cursor, and a level change moves it to the level shown
- [x] Levels below seen however deep: each visible tile is drawn once, from the level seen there (`map_view_level()`, no more 6-level limit, for zones and selection too), veiled a little more with each level but never black; 4 times the frame rate at the surface level (21 to 82 FPS in a software-rendered test window), steady whatever the height
- [x] Measuring tools: `bench_simulation` in processor time with percentiles, slowest ticks (with perf time ranges: `BENCH_TIMESTAMPS`) and region rebuilds; `--bench-render` and `--no-vsync` in the game, drawing time in the F3 overlay; `profile` preset for perf
- [x] Scans bounded by boxes (designations, gathering zones, connectivity regions; hearth warmth stops after the last station): `bench_simulation` scenario 3 from 49 to 39 µs per tick, scenario 1 from 34 to 26, same state hashes
- [x] Tick spikes (scenario 3, 64 colonists, worst tick 30 to 60 ms → 12 to 17 ms, p99.99 6.4 → 4.8 ms): work searches head for the box of the orders or zones (A*, `pathfinder_find_nearest_in_box()`); early retries after a change spread over 8 ticks, each settler on its own; each settler's night starts and ends a little later after its slot (up to a quarter of an hour), so that 64 settlers no longer eat, go to bed or look for work on the same tick; a walk to one's own bed is a plain A*; pathfinder nodes packed per tile
- [ ] Found along the way (performance): the slowest ticks left (12 to 17 ms, scenario 3) are a few far food, water and gathering searches on one tick (5,000 to 17,000 tiles each); the gathering predicates (`gather_tile_job_in()`, evaluated four times per tile reached) and `map_move_cost_at()` are a large part of a search; the temperature sweep (12 to 18 % of a tick) is the largest cost left; `job_still_valid()` recomputes the colonist's gathering jobs every tick
- [x] Ground bedding: a 1 × 2 sleeping place that costs no material (a few minutes of work), resting as the bare ground does ("slept on the ground" still applies), so that settlers without a bed sleep near the camp
- [x] Quality of built things from the builder's level and the materials (capped at the materials' average grade + 1, grass and twigs do not cap); hit points by grade (bed 20, wall 60, screen 40 at standard, ×0.6 to ×1.5); a grass bed loses 1 hit point a day, gets a refresh site below half (4 dry grass, 125 ticks) and falls apart at 0; grade and hit points in the tile inspector
- [ ] Built things, later: comfort by bed grade and the "slept on old bedding" mood (with moods); damage from fire, weather and animals; repairing walls and screens
- [x] Beds owned by one settler (taken the first night, kept, freed when the bed or the settler goes; the owner in the tile inspector, the bed in the colonist inspector); beds of 1 × 2 tiles, head and foot, turned with a key (F on QWERTY and AZERTY), laid side by side when a rectangle is dragged
- [ ] Beds, later: the player assigns a bed to a settler (or frees it); double beds for couples (with relationships)
- [x] Haulers bring materials to construction sites (builders still fetch what nobody brings); items promised to a site so that several settlers never bring the same ones; a construction scenario in `bench_simulation` (scenario 3)
- [x] Profile `bench_simulation` scenario 3 (64 colonists, 5 days: 2.8 ms → 40 µs per tick, worst tick 1.4 s → 15 ms): exact haul pre-check, region scans before fetching and bed searches, stamps narrowed per reservation kind, a minimum delay between failed work searches, regions refreshed before each colonist
- [x] Remaining scenario 3 spikes: wake-ups and searches spread over ticks (see the tick spikes item above)
- [ ] The pre-check scans cost grows with the map size (keep a per-region count of haulable stacks if it shows up on large maps)
- [x] Workstations built through construction sites: their blueprints are sites (haulers bring the materials, builders work from a neighboring tile, same inspector, cancelled like other sites, "build another" works on them); the station appears once the site is finished
- [x] Roofs: a grass roof built like any structure, tile by tile over anything (bed, wall, stockpile), resting on a wall or rock within 2 tiles (planned walls count, the work waits for them); natural rock roof in caves
- [x] Roof layer: roofs and their sites only drawn in the roof view (Y, or while using a roof tool), roofs planned over the sites of walls and beds, an order of its own to take roofs down, the cancel tool working on one layer at a time; a roof falls in when its last support in reach goes (half its materials drop, a notice) (replaced by roofs as floors, see *Levels*)
- [ ] Clearing cave floors, once mining exists (rock cannot be mined yet: it needs a pick)
- [x] Knowledge spreading: conversation (nearby settlers chat now and then, a speech bubble, a little learned from what the other talks about, never past 30), apprenticeship (a settler lacking the level for a job assists a master doing it: learns up to the master's level, the work goes 25% faster, one apprentice per master), fading after 15 days without practice (down to 80% of the best level); only discovered knowledges in the colonist inspector, a tooltip per knowledge (gains of the day by source, practice, fading, masters); notices (a knowledge left to a single master, lost, forgotten, mastered again)
- [x] Fireside stories and the "teach" mark (with the open hearth, M4)
- [ ] Knowledge spreading, later: related practice and inventing knowledges (with the first knowledges that are not starting ones); a colony log keeping the notices; chats feeding relationships (social system)
- [x] UI framework: buttons, panels, tooltips, windows sized to their content (title bar to drag, × to close, stacking order, scroll bar and wheel when too tall), measured layouts that adapt to text length
- [x] Bottom left menus: Build (workstations, zones, orders as icon buttons with tooltips: name, description, cost, shortcut), Work, ? (controls help, F1); the tile inspector above them; Escape or a right click (not a pan) steps back (drag under way, tool, menu, front window) and never quits; A (AZERTY) / Q (QWERTY) builds another of what is selected (zone, workstation, structure; structures and sites can be selected)
- [x] Forbid and allow: items, plants (a tree from any tile of its trunk or foliage), structures, roofs and workstations the settlers must leave alone (nothing picked up, hauled, stored on, eaten, slept in, used, taken apart, refreshed, cut, gathered or felled; jobs under way on them are dropped, a settler asleep in a bed forbidden meanwhile sleeps on), forbidden doors and ladders locked (nobody goes through); a button per thing in a tile window (a click on a tile selects what lies there; clicking again goes through station, tile, zones, grass) and in the workstation window, Forbid/Allow rectangle tools in the Orders menu (grass left out), a "no entry" sign on what is forbidden
- [ ] Forbid, later: new items forbidden by default where the player chooses (dropped by raiders, outside the colony), forbidding the stacks of a whole stockpile at once
- [ ] UI: horizontal scrolling (the work grid is wider than small screens), windows remembering their position between sessions, a shortcut shown for the workstation buttons (E cycles them)

## M4: Survival

- [x] Fire: the open hearth (dug, no material), fuel (branches, twigs, logs by weight, up to 20 kg, 2.5 kg an hour; haulers refuel it below half; "keep it burning" and a fuel filter), lighting (whoever lights a hearth with no fuel in it fetches some first; embers carried from a burning fire within 60, else friction with a handful of grass as tinder, 30% to 100% by level), light around it at night (6 tiles), fire hardening of spears (×1.5 hit points, grade and condition kept), fireside stories in the evening (a master tells about a knowledge marked to teach, up to 8 listeners learn up to 60)
- [ ] Fire, later: a torch (light to work at night); grilling meat and cooking (roots are roasted already); fire spreading and rain putting fires out; hearths that block the way (they are walked over for now); hardened spears dealing ×1.2 damage (with hunting); the better storyteller taking over at a hearth
- [x] Temperature per tile: outdoor air through the seasons and the day (12, 22, 10 and −2 °C in the middle of each season, ±5 °C over the day), heat spreading between tiles (walls and doors slow it down), open sky, roofs and natural roofs (caves stay near 10 °C), burning hearths heating (a closed and roofed hut with a fire stays about 15 °C above the air); the warmth a fire radiates (+20 °C felt one tile away, +9 at two, +4 at three, stopped by walls), added to the air's in a felt temperature; sleeping below 10 °C or above 30 °C felt rests less (2.5% per degree, down to half); settlers with no bed who would sleep too cold or hot walk to a spot beside a fire that feels right, and move there in the night when it gets colder; the temperature (air and felt) in the tile inspector, outdoors under the clock, a felt temperature view (H)
- [ ] Temperature, later: hot weather; clothes; warmth spots taken by priority (the coldest settler first) and beds placed by the fire; moods, hypothermia and heatstroke (with moods and health); a roof's insulation by its material
- [x] Food through the seasons, step A: plants follow the seasons (food ripening on its days of the year and gone after them, plants growing from spring to autumn, wild garlic and nettles withering out of season); the berry bush replaced by eight wild plants: bramble (blackberries at the end of summer, slow to walk through), hazel (hazelnuts in autumn, branches and twigs), wild garlic, nettle, ribwort plantain (greens), wild carrot and cattail (roots dug up; cattails grow in the shallows, gathering zones now cover shallow water), mushrooms (appear under mature trees in autumn, gone within days); picking and digging jobs, careful ones (mushrooms, wild garlic, wild carrot) from Gathering 60 and never eaten in place; the digging stick (crafting spot, and fire hardening at the hearth), fetched on the way to dig; food stocks, withered plants and food out of season in the tile inspector; sprites for every plant; the game starts at the end of summer, with brambles bearing fruit and hazels near the arrival point
- [x] Food through the seasons, step B: food lying on a tile rots (loses hit points, gone when none are left): greens and mushrooms in 2 days at 15 °C, blackberries 3, roasted roots 4, rhizomes 15, wild carrots 20, hazelnuts 90; twice as fast every 10 °C warmer, stopped when frozen, slower under a roof; time left shown in the tile inspector; roasting cattail rhizomes and wild carrots at a burning hearth (more food, 4 at a time)
- [x] Settlers eat food about to rot first: food on the ground that rots away within a day, within a short walk, before nearer food that keeps
- [x] Gathering zone window redone: resources in foldable groups (wood: trees, branches, twigs; food; stones; grass alone) whose order limits each of their resources (both must want it); a mode dropdown (forever, gather N, until N) with the target between −/+ (Shift: ±10); food until N days of food for all the settlers (the food group: until 5 days by default, gathering again as soon as the colony has less); "wait for the full harvest" for every plant resource (food, branches, twigs: a full stock, every fruit ripe; trees and grass fully grown), a plant-with-rays icon with a tooltip, tri-state for a group and for the whole zone
- [ ] Food, later: storage pits; smoking and drying; food quality affects mood
- [ ] Food, later: poisoning by a mistaken plant below Gathering 65 (with health); meal variety (greens) in moods; plants spreading and seeding (picked-out areas only grow back from what is left)
- [x] Weather: the sky every 6 hours (clear, cloudy, fog, rain, storm; snow and snowstorms when cold) with odds per season and plausible changes (one step at a time, fog only at night and in the morning), warm and cold spells over a few days, drawn from the seed (not the game's random stream); clear skies make warmer days and colder nights, clouds even them out, rain and storms cool the air; settlers out in the rain get wet (feel up to 8 °C colder, dry fast beside a fire); rain puts out fires under the open sky and stops anyone lighting them; food rots faster and fresh grass does not dry out in the rain; mushrooms come after rain; snow cover on open ground; the sky, snow cover and wetness in the HUD; rain, snow, fog and lightning drawn over the view, clouds dim the day; debug buttons for the next sky and a cold snap
- [ ] Weather, later: settlers taking shelter when at risk of hypothermia (with health); snow slowing walking and covering plants; rain filling puddles and making work outdoors slower; wind (direction, spreading fire, wind chill); droughts and floods; sun and heat speeding drying; moods (soaked, enjoying the sun)
- [ ] Bladder and bowel needs, hygiene, latrines
- [ ] Containers (buckets) and roles: water carrier filling buckets near dwellings
- [x] Moods, step A: a mood from 0 to 100 drifting towards the sum of thoughts; states (hungry, thirsty, tired, cold, too hot, soaked, out in the rain) and memories (how the last night went: in a bed, on the ground, cold, hot or wet; varied, monotonous, cooked or spoiled meals; warmed by a fire; a fireside story; chats); mood speeds work up or slows it down (±20 %), changes quality and condition (±10 levels) and learning (±25 %), replacing the slower work after a night on the ground; breaks below 25 (sulking without working) and 10 (going off alone, binging on the colony's food), a notice, relief afterwards; mood, its effects, thoughts and breaks in the colonist inspector, faces from the mood; a debug button to sadden everyone
- [x] Moods, fading thoughts: no all-or-nothing thought and no drift, the mood is 50 plus what every thought weighs now; states set in over an hour, hunger, thirst and exhaustion grow worse the longer they last, and a state over fades; memories weigh in full for a while, then halve at their own pace until forgotten; renewed or adding up (chats) when they come back; the inspector shows what each thought weighs and the most it weighed while it fades
- [ ] Moods, later: expectations rising with the colony's wealth; inspiration in a very good mood; destructive breaks; clothes, deaths and injuries, meal quality; the mood in the work grid
- [x] Attributes, step A (`docs/ATTRIBUTES.md`): continuous values on a bell curve, no all-or-nothing traits; body (walking, work pace, learning, sleep, appetite, cold and heat tolerance) and mind (outlook, temperament); drawn when a settler arrives, from a stream of their own; every attribute in words (five bands) in a table in the new Character tab of the colonist inspector, with a tooltip; thoughts shown with the temperament counted
- [ ] Attributes, later: kindness (compliments and insults), social ease, self-confidence, beauty and sexuality (with relationships); sociability and demands (with the social need and expectations); toughness and pain tolerance (with health); courage (with danger); inherited (with genetics)

- [x] Inspector tabs as tables and meters: Needs as a bar per need (red to green, white in the middle), Mood as a bar then a table of thoughts (value red or green, a tooltip tells what it is about, its original value and how it fades), Character and Knowledge as tables (value on the right, in color); exact values and effects only in tooltips

## UI redesign ✅

The interface took its final layout (`docs/UI.md`: screen map, behaviors,
theme and migration plan).

- [x] Foundations: theme (color roles, sizes, UI scale with `--ui-scale`), screen zones; tooltips wrap their long lines; floating windows stay above the bottom bar; names of keys without a character translated (Esc/Échap, Space/Espace...)
- [x] Bottom bar (Build, Work, Menu), three-level build menu (category column, icon row with shortcut letters, unavailable icons veiled) with its card (hovered icon or tool in use: name, cost, description, how to use it one line each, shortcut, why unavailable), game menu (resume, controls, language, quit; pauses the game; Escape opens it when nothing else is left to close); the debug readout (F3) moved top right
- [x] Bottom right: temperature overlay and speed buttons (drawn icons, tooltips with shortcuts), vertical level slider (drag, +/−, "level/levels" counted from 1, a tick at the ground level); date, season, temperature and weather right aligned beside it, "Paused" there too
- [x] Colonist bar (portrait, name, current action; click selects, Shift adds, double click centers the view; shrinks, dropping the action then the name, then wraps onto rows: 64 colonists fit)
- [x] Alerts: a core module looking at the game afresh (critical food, water or rest, food for less than a day, colonists breaking down, without a bed), listed above the date with a tooltip and a click that jumps to the first colonist concerned
- [x] Inspector box with tabs and action buttons for every selection: colonist (summary; Needs, Mood, Knowledge, Gear, Social), several colonists (their names), workstation (Bills, Fire), stockpile (Storage), gathering zone (Resources), tile; actions forbid/allow, build another, delete as icon buttons with tooltips; docked windows that scroll; the floating inspectors are gone
- [x] Work grid as a main window; messages under the colonist bar; debug readout top right
- [x] Resources list (top left): the stock by item category with totals, groups folded with a click, an icon per item, tooltips with where the items lie (stockpiles, workstations, haulers), their weight and the days of food; stops above the inspector, the work grid and the build menu (the lines left out are counted)
- [ ] Colonist bar with many colonists at a large UI scale (64 at 150 % on 1280 × 720: eight rows reaching the date and leaving the work grid no room): let the entries shrink further, or show a row of initials beyond some rows
- [x] Settings window from the game menu (instead of its Language button): interface size 75 to 200 % applied at once (the font opened again at its size, windows placed again), colorblind-friendly palette (blues for lit and checked, a blue to orange temperature view), language; saved in `settings.cfg` in the user's data folder (`core/settings`: a forgiving `key = value` text), the command line winning for the session
- [ ] Colorblind palette, later: the work grid's priority shades and the map's markers (forbidden, zones) checked with a simulator once they settle
- [x] Colony log: a main window (Log button, J) with the last 200 notices and their dates
- [ ] Alerts, later: idle colonists, and the alerts of future systems (enemies, fire, unmet orders); a colonist's own Log tab in the inspector

## M5: Vision and exploration

- [x] Design of vision and the fog of war (`docs/VISION.md`)
- [x] Vision, step 1: sight in 3D per settler with no range limit (only rock, walls and floors stop it; thin foliage does not), peeking round corners (five eye points), fields of view whose cost grows with what is seen (prototype and benchmark first), near view refreshed on a new tile, on sleep and waking and on a change nearby, the rest after a few tiles or seconds, spread over ticks; a count per tile of who sees it; debug overlay of the selected settler's view; nothing hidden yet
- [x] Vision, step 2: the colony's picture (known world: a copy of the layers, unexplored tiles unknown), the start (view from the arrival point and the path from a random map edge, guarantees in view), drawing (unexplored black, remembered under a veil, seen live), tile readout, tools refusing unexplored tiles, Reveal map and See everything cheats (See everything by default in tests and benchmarks that are not about vision)
- [x] Vision, step 3: settlers act on the picture: every path and search on the known map and items, targets re-checked against the real world only while in view, stock, food alert, bills and gathering targets from the known items, messages about the map only when seen
- [ ] Direct move orders for settlers (also how the player explores the fog; there is no Explore order)
- [ ] Orders on a workstation destroyed out of view: keep its bills until someone sees it gone (with enemies)
- [ ] Vision, step 5: measure (bench_simulation and --bench-render with vision on), tune, checks on screen
- [ ] Vision, later: darkness shortens sight, eyesight attribute, lookouts (trees, towers), hearing, saving the picture (M7)
- [ ] Wildlife, hunting

## M6: Danger

- [ ] Event director
- [ ] Health: injuries, treatment, disease; consequences of hunger, thirst and exhaustion
- [ ] Combat basics (line of sight, cover), raids, animal attacks

## M7: Persistence and polish

- [ ] Save/load (versioned), autosave
- [ ] Settings menu (language, resolution, volume, key bindings) persisted in the user preferences folder
- [ ] Main menu, new game options (seed, map size, difficulty)
- [ ] Sound and music
- [x] Sprites (128 × 128 pixels per tile) in a texture atlas with halved copies for zoomed-out views: trees, hazels and brambles (5 growth stages, hazels and brambles with and without fruit), low plants (3 stages), mushrooms, colonists in 4 layers (naked limbless body in 3 shapes, head with 4 expressions, beard, hair) in front view, a moving fist (with the tool of the job) while working
- [x] Ground textures, the same on every tile of a terrain: soil, rich soil, shallow and deep water seen from above; grass over the ground, taller with its growth; the cut face of granite, sandstone and earth walls, dark and compact so that they never read as ground
- [x] Textures for sand, gravel, rough stone, foliage (leaf canopy) and grass roofs (thatch)
- [ ] Transitions between terrains
- [ ] Colonist sprites seen from the back and the sides, walking and sleeping poses
- [ ] Sprites for items, workstations and structures
- [ ] Clothes as sprite layers over the body (hide tunic, fur wrap...), once clothing exists
- [ ] Gender (or body types) for colonists, to tie beards and names together
- [ ] Data-driven definitions (terrain, plants, items) + mod loading

## Living world (long term, design under discussion)

See `docs/GAME_DESIGN.md`, *Living world*. Target map: 256 × 256 tiles.

- [ ] **Levels (priority)**: several levels below and above the ground: natural relief (hills, cliffs, caves), dug mines and cellars, buildings with storeys. Comes before wildlife, vision and combat, which all depend on it (map API, pathfinding, regions, temperature, roofs, rendering, UI); the rest of M4 waits. Steps: (1) the map becomes a volume with a single level used, behavior unchanged; (2) relief, paths and regions in the volume, view by level; (3) digging and building: mines, ramps, ladders, stairs, storeys, support, collapses, falls, climbing trees; (4) temperature, light, roofs and rain in the volume; (5) flowing water
- [x] Levels, step 1: the map is a volume (`depth` levels, up to 256 × 256 × 32, level-major layers), a level on every tile, position and per-tile cache, searches, regions and heat kept within a level, the level shown in the camera; world generation still makes one level, hashes unchanged
- [x] Levels, step 2: relief (plains and water on level 0, hills rising level by level, rock outcrops behind cliffs, earth or rock inside the ground), floors and open space, natural slopes walked up and down (×2 cost), paths, regions, settlers' searches and hauling across levels, natural roof = ground or rock overhead, open space as outdoor air for heat (and skipped by the heat sweep), level shown chosen with Page Up/Down or Shift+wheel, lower levels seen blurred and darker through open space, slopes and drops drawn; flat worlds kept for tests and benchmark comparisons
- [x] Levels below the plains: 4 underground levels (numbered from the plains, negative below), lakes and rivers in hollows (shallows and beaches one level down, deep water two, banks walked up by slopes), earth then rock under the ground; haulers skip levels without items (the construction benchmark back to 49 µs a tick with 64 settlers, from 57 before the underground and 69 with it)
- [x] Levels, step 3a: digging earth (galleries into hills, dugouts) and cutting ramps into one-level banks of earth, with the shelter builders' knowledge, faster with a digging stick fetched on the way; rock stays for a future mining knowledge; the "Mining" work column becomes "Digging"
- [x] Levels, step 3b: ceilings held up by rock, walls or branch props within 2 tiles (the roofs' rule); diggers wait for support instead of bringing a ceiling down (the tile inspector says so); a support taken away: the gallery caves in (filled up again, what lay there pushed aside, a notice); settlers whose ground goes fall to the level below (no harm until health); every level checked when the change logs overflow (roofs only looked at level 0)
- [x] Levels, step 3c: branch ladders (climbed at ×3, planned only where they lead up: rock cliffs, pits) and digging down (the ground and the earth under it; the hole slopes up to where its digger stood, the way into a cellar, or stays a pit for a ladder)
- [x] Levels, roofs as floors: a roof is the floor of the open tile over a hut, on the level above its walls, too frail to stand on; planned there (no more roof view) and built from below, overhead; taken down with *Take apart* on its level, from below; it follows the ceilings' rule (falls in without support, diggers leave the rock that holds it); one open level over the highest ground for roofs (flat worlds get two levels)
- [x] Levels, step 3d: trees as buildings: saplings stay plants walked through; from stage 2 a trunk of solid wood (1 × 1; 2 × 2 old and 3 × 3 ancient trees, rare, from world generation only) and floors of foliage walked on around it on the upper levels (2 to 7 levels in all), drawn like walls and floors (the trunk's cut face, leaves, shade under the foliage); growth waits while something is in the way; climbing up and down a trunk at ×3 from a tile beside it; trees spaced out by world generation; no roof through a tree; felled trees fall full length (2 tiles per level, a band as wide as the trunk) where aimed: away from structures, else the least harmful side (structures hit lose half their hit points, with a notice); wood along the trunk (by the trunk's area), the crown's branches and twigs where it lands, falling to the ground below over a drop, and what lay on the foliage falls too; `TREE_MAX_LEVELS` (7) open levels over the highest ground
- [x] Trees shed twigs (young ones) and branches (grown ones) at random, onto the ground under their crown and onto their foliage; a new world has some lying around its trees; they lie on map layers (`Map.branches`, `Map.twigs`) and gathering zones take them from the tiles they cover only: a zone on the ground never sends settlers up a tree, a zone drawn over the foliage does (the tree no longer holds a stock of its own)
- [x] Ragged crowns and foliage densities: the foliage outline is no longer a perfect square or disc (tiles missing more and more towards the edge, never next to the trunk, a different crown per tree from the foot's position, a growing tree only adds foliage, and the crown grows tile by tile with the tree: the foliage over the trunk first, then nearest tiles first on every level out to 2 tiles from the trunk, each coming in sparse and thickening with age; the trunk rises to 2 then 3 levels at 0.49 and 0.55 of growth); the branches thin out from the trunk in three densities (dense next to it, then thin, then sparse; terrains `TERRAIN_FOLIAGE`, `_MEDIUM`, `_SPARSE`, textures for each): only the dense foliage is walked on, what falls on the thinner goes through to the floor below
- [x] Debug menu (F4): plant a tree seedling and grow a tree a tenth of its growth per click (Shift: all the way), from any tile of the tree; the trunk and crown are written into the map at once (`game_debug_plant_tree()`, `game_debug_grow_tree()`)
- [x] Fixed: a tree standing in the map stopped growing just before the stage where its trunk rises a level (its own foliage was no longer recognised as such by `tree_room_to_grow()`)
- [x] Hazel as a tree (replaces the hazel shrub, a shrub sprite as a seedling): a small tree (two levels of trunk at most, less wood); its hazelnuts fall in autumn into a map layer like the twigs (`Map.nuts`, under its crown and on its dense foliage), picked up by gathering zones (`GATHER_JOB_NUTS`) and eaten by hungry settlers where they stand; nuts have no state of their own and rot on average (the tile's rot rate, 90 days at 15 °C, rolled from the seed). Nothing bears branches or twigs on the plant itself any more
- [x] Thin foliage seen through: the medium and sparse foliage show the levels below (textures of leaves and bare branches on a transparent background; `map_view_level()` goes through them)
- [x] Levels, fixes: zones drawn on the level shown (they were drawn from level 0's layer, so not at all over raised ground), workstations kept on their level (they were all put on level 0: not drawn, not reached, nor freed when removed), hearth light and fireside stories on their own level; `tests/test_levels.c` checks zones and stations on raised levels (the other tests use flat maps, ground on level 0)
- [x] Levels: ramps (natural and dug) marked with a triangle on the level shown, pointing up where the ramp leads up, down over one that leads down (`map_ramp_at()`)
- [x] Order to fell trees (on the tree's foot, from any tile of its trunk; nobody can carry it out until woodcutting and axes exist) and a debug cheat felling and mining marked trees and rock at once
- [ ] Levels, deferred from step 3d: foliage as a shelter from rain (it closes no room for now); other uses of climbing trees (a lookout, a refuge): wait for vision and wildlife; felling itself waits for the woodcutting knowledge and an axe (the order exists, only the debug cheat carries it out); with felling: a warning on a chop order whose tree has no clear side (only a notice after the fall for now), and the fallen trunk lying across its tiles until limbed and cut into logs (logs are dropped right away for now)
- [ ] Levels, later with the building knowledge: floors, storeys and stairs (plank floors walked on, built like roofs: from below, on the level above the walls)
- [ ] Levels, found along the way (roofs): a shortcut from the roof tool to the level above; roofs drawn see-through (or hidden) from the level above, to see the hut under them
- [x] Found along the way (build): SDL3_ttf cached `CMAKE_POSITION_INDEPENDENT_CODE=ON`, so the core was built with `-fPIC` (exported functions no longer inlined): turned off for our targets, `bench_simulation` ~13 % faster
- [ ] Found along the way (performance): now that trees keep growing through every stage, each trunk that rises closes the foliage tile over its top, and a change that closes a tile makes `regions_update()` rebuild every connectivity region (a few ms, up to ~30 ms on the benchmark map): regions now merge incrementally when a new floor joins several, and a trunk rising into thin foliage logs nothing, which leaves 21 rebuilds in `bench_simulation` scenario 1's 10 days (it was once before trees grew past stage 3); handle the remaining closings incrementally
- [ ] Found along the way (performance): the open levels over the highest ground cost (18 levels instead of 14 since trees take room: `bench_simulation` scenario 3 went from ~48 to ~65 µs per tick, scenario 1 from ~35 to ~37): temperatures, the designation and haul scans and mushrooms still go over every level (items already skip empty levels, plants stop at the highest ground); skip levels with nothing on them
- [ ] Levels, found along the way (support): falls and cave-ins hurt once health exists (M6); stone pillars and timber props once wood is cut; a warning on orders that take a support away
- [ ] Levels, found along the way: click through open space to act on the visible ground below; orders and labels of the lower levels in the blurred view; a colonist list that jumps to a settler's level; heat across levels (step 4); the inside of hills on the level shown is drawn as plain rock or earth walls (show it as the hill rising above instead)
- [ ] Levels, decided: 2 m per level (one storey), hills but no mountains, around 32 levels; trees span several levels (a trunk of wood to climb, floors of foliage above); a felled tree falls full length, always exactly where aimed (away from buildings, else the least harmful side, with a warning), then the trunk is limbed and cut into logs; current level sharp, lower levels blurred; natural slopes, dug ramps, ladders and stairs; nothing overhangs more than 2 tiles from its support (mines and storeys collapse otherwise); water flows between levels
- [ ] Aging speed chosen per game (up to ×10, the same for settlers and animals; plants follow the seasons): generations within a game (with pregnancy, children, aging)
- [ ] Soil: moisture and fertility per tile; light from the sun, the canopy and roofs
- [ ] Vegetation spreading: seeds by wind and animals, roots and runners; competition for light, water and fertile soil
- [ ] Wildlife as a living population: eating plants, hunting, fighting, breeding, dying of old age (extends M5 "Wildlife, hunting")
- [ ] Health by body part: injuries, bleeding, infection, lost limbs, for settlers and animals (extends M6 "Health")
- [ ] Genetics: inherited traits with random mutations for plants (varieties), animals and settlers; selection by the player (crops, breeding)
- [ ] Performance: spread long work over ticks (budget per tick for path and job searches) so that no single tick drops a frame

## Later

- [ ] Social life: relationships, sexual needs (abstract representation)
- [ ] Tastes (`docs/ATTRIBUTES.md` §6: drugs, foods, animals, women or men, kinds of work); related practice and the knowledges after the starting ones; classes with classrooms and schedules
- [ ] Content of the later historical periods (Mesolithic to Iron Age, then beyond)
- [ ] Earth building with settling down: clay deposits, wattle and daub on the branch frame of grass walls, then cob, mud bricks, rammed earth, fired bricks (`docs/KNOWLEDGE.md` §13)
- [ ] Pregnancy, children, aging
- [ ] Religion, spirituality levels, "inner peace" ending; other endings

## Technical backlog

- [ ] Remember the chosen language between sessions
- [x] Every keyboard control by physical position (scancodes): the same layout on QWERTY, AZERTY and others, labels shown from the active layout
- [ ] HarfBuzz + font fallback when a translation needs non-Latin scripts (would also allow re-enabling kerning, disabled because SDL_ttf mispositions kerned glyphs without HarfBuzz)
- [ ] More plural rules (Slavic languages, Arabic) when needed
- [x] Batch tile rendering (geometry) for large maps at low zoom; sprites come from a texture atlas
- [x] Pathfinding benchmark (`bench/bench_pathfinding`)
- [x] Connectivity regions: no search for unreachable goals, water or orders (`core/region.h`)
- [x] Failed food searches no longer explore everything within their cost limit: a scan of the colonist's region, limited to the square the search could reach, proves first that some food is there
- [ ] World generation: guarantee drinkable water near the start (small maps can have none; the spawn point only prefers water)
- [x] Simulation benchmark (`bench/bench_simulation`: many colonists, long runs, state hash)
- [ ] Windows and macOS CI builds
- [ ] Choose a license
- [x] Debug menu (F4): instant free construction, free crafts, work without tools, instant taking apart (`GameDebug`); items menu (F5): every item of the game, one or a full stack, dropped on the clicked tile
- [x] Test tools: `--debug` (cheats from the start), `--scenario` (the benchmark's situations in the game, `core/scenario.h`), `--colonists`; everyone knows everything, skip an hour or a day, next season, spawn items, move settlers (debug tool)
