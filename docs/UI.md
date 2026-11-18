# User interface

The target layout of the game's interface, decided with the project owner,
and the plan to get there. The interface is built in its final places now,
even though most buttons do not exist yet: a button that exists must already
sit where it will stay. This document is the reference for every new window
or button; `docs/ARCHITECTURE.md` describes how the code is organized.

## Principles

- **Mouse first, with keyboard shortcuts.** Every main button and every tool
  has a shortcut (a physical key position, `APP_KEY_*`), shown in its tooltip
  or card. Shortcuts may become rebindable later.
- **Text buttons for the main menus**, clearer than icons. Icons only for the
  last level of the build menu (things to build, orders), with the
  shortcut letter in a corner.
- **One main window open at a time** (Work, later Schedule, Assign...):
  opening one closes the other and closes the build menu. Escape or a right
  click steps back one level (drag, tool, menu, front window, selection);
  with nothing left to close, Escape opens the game menu. The *Move* action
  in the action menu of the selected settlers arms a tool: each left click
  on the map sends them there (`game_order_move()`), until Escape or a right
  click puts it away. Their path and the tile they go to are drawn.
- **Never on top of each other.** Each zone of the screen has its own
  content; zones are computed in one place (`game/layout.h`) from the screen
  size and the theme.
- **Readable at a glance, detailed on demand**: a short summary always
  visible, details in a tab or a tooltip.
- **Minimum resolution 1280 × 720.** The interface must also hold 64
  colonists (the benchmark's count, kept for tests), even though a real game
  will rarely have that many.
- **Original look.** The layout follows the conventions of the genre (dark
  translucent panels, flat buttons, thin borders), with our own colors and
  art; nothing is copied from another game.

## Screen map

```
+--------------------------------------------------------------------------+
| Resources   [ colonist bar: portrait, name, current action ... ]   Debug |
| (folding    Messages                                               (F3)  |
|  groups)                                                                 |
|                                                                          |
|                              (the map)                                   |
|                                                                Level     |
| [tab panel: Needs, Mood, ...]                                  slider    |
| [tabs: Needs | Mood | ... ]                         Alerts       ^     |
| +-------------------------+                         Weather      |     |
| | Inspector: summary      | [action buttons]        Temperature  |     |
| |                         |                         Date, hour   v     |
| +-------------------------+                   [overlays] [speeds] 4/9  |
| Build  | Work  | (Schedule) | (Assign) | ... | Log               | Menu  |
+--------------------------------------------------------------------------+
```

### Bottom bar

- A full-width bar along the bottom edge. Text buttons of the same width,
  left aligned: **Build** (`menu.build`), **Work** (`menu.work`), and later
  Schedule, Assign, Animals, Wildlife, Research, in that order, then
  **Log** (`menu.log`, key J) last: with the others missing it sits right
  after Work for now.
- **Menu** at the right end: opens the game menu (same as Escape with nothing
  else to close).
- A button stays lit while its menu or window is open; its tooltip gives its
  name, a description and its shortcut.

### Build menu

Three levels, opened from the bottom left corner:

1. **Build** in the bottom bar opens a **vertical column of category
   buttons** above it: Workstations, Construction, Zones, Orders (in two
   columns once there are many).
2. A category opens a **horizontal row of icon buttons** to the right of the
   column, along the top of the bottom bar: everything of that category,
   with its shortcut letter in the top left corner. Clicking an icon picks
   the tool; the menus stay open.
3. A **card above the category column** describes the icon under the mouse,
   or else the tool in use: name, cost ("5 Branches and 24 Dry grass"), the
   work and the knowledge it takes ("Work: Construction", "Knowledge: Basic
   shelters (50)"), description, and why it is unavailable. It carries no
   controls (no shortcut, drag or Esc hints: a shortcut is drawn in the icon's
   corner). The column and the card are `Theme.build_menu_width` wide. It
   replaces the tooltips of the icons and the old "active tool" panel at the
   top of the screen; it also shows (directly above the bottom bar) when a
   tool is picked with its shortcut, menu closed.

### Colonist bar (top center)

- One entry per colonist: a portrait (the colonist's sprite), the name, and
  the current action (cut short, in full in the tooltip). Room is kept for
  markers later (mood, hunger, sleeping...).
- Click: select the colonist. Double click: select and center the camera on
  it. Shift + click: add to the selection.
- With many colonists the entries shrink, then wrap onto more rows.

### Inspector (bottom left)

A fixed-width box above the bottom bar, shown when something is selected:

- **Summary** of the selection, always visible.
- **Tabs above it**, depending on what is selected; a tab opens its panel
  above the tabs (one at a time, click again to close). The panel has no
  title (the tab's button says it): only a × at the top right, with a strip
  of its own at the right of the rows. The box and the
  panel are as wide as the row of tabs when it is wider than the default.
- **Action menu to the right** of the box: an icon button per action on what
  is selected (later: draft, attack...; now: forbid/allow, fell, harvest,
  uproot, take apart, cancel a blueprint, build another, delete a zone or a
  workstation...), each with a tooltip and its shortcut. An order button is
  lit while the order is given (a click takes it back) and veiled while it
  cannot be given (nobody knows how, nothing to harvest yet: the tooltip
  says why).
  Every selectable thing has such a window, titled with its name:
  workstations, zones, and each thing on a tile (structure, roof, blueprint,
  items, plant or tree, grass), one at a time (a click again on the tile
  goes to the next one). A click reaches everything seen on the tile: on the
  level shown, then on the ground seen below it through open space (a zone or
  a stack seen from a level higher), top first.

The same box serves every kind of selection; the floating windows of today
become tabs:

| Selection           | Summary                                       | Tabs (now; later)                                 | Actions                       |
|---------------------|-----------------------------------------------|---------------------------------------------------|-------------------------------|
| One colonist        | Name, age, current action, needs and mood at a glance | Needs, Health, Mood, Character, Knowledge, Gear, Social; Log | Move (later: draft, attack)   |
| Several colonists   | How many, their names                         | —                                                 | Move                          |
| Workstation         | Name, state (forbidden, fire, work in progress), bill count | Bills; Fire (hearth)                 | Forbid, build another, delete |
| Stockpile zone      | Name, priority, size, "left alone" when forbidden | Storage (filters)                             | Forbid, build another, delete |
| Gathering zone      | Name, priority, size, "left alone" when forbidden | Resources                                     | Forbid, build another, delete |
| Tile                | What lies there: structure, roof, site, items, plant, each with Forbid/Allow | —                          | Build another; items: Carry elsewhere |
| Wild animal         | What it is called (a hind, a stag) as the title; species, sex, activity, needs as bars (tooltips), herd, health (as the Health tab) | — | (later: hunt)                 |

The animal's window has a Character tab like the settlers'.

#### Bills (workstations)

The Bills tab holds an **Add a bill** button (lit while its window is open) and
the active bills. The tab's panel and the windows opening at its right are
always as tall as the screen allows (up to the colonist bar), with the same
top and bottom; each is docked at the right of the one before:

1. **Recipes to add** (the button): one row per recipe of the station: the
   picture of what it makes, its name, and its price as a picture per
   ingredient with the count under it (alternatives, such as flint or cobble,
   separated by "/"; the tooltip of a picture names the item). A click picks
   the recipe, a double click adds it as a bill.
2. **Details of the picked recipe**: a table (work, knowledge and level,
   ingredients as text, time in game time) then the thing made (name, weight,
   bulk) The tooltip of the knowledge says what it does
   for this recipe: the level needed, the work speed, and, when they depend on
   the level, the quantity, quality and condition of what is made and the
   chance of failure, at the lowest level that can make it and at 100 (figures
   from the core: `knowledge_work_speed()`, `recipe_craft()` without luck).

An active bill in the list has two rows: its name with the **pause** and
**remove** buttons at the right (the name is red, with the problem in a
tooltip, when nobody can work on it), and how often it is done (mode, −/+ for
the target). A click on it opens its **details** at the right, in place of the
list of recipes, with the same details of the recipe (2) at the right of it:
the ingredients and the tools (red, with a tooltip, when
nothing suitable lies in the colony or a tool cannot be had); the assigned
colonist, a drop-down list (*Anyone*; *Colonist skilled in [knowledge]*, which
adds a slider with two handles, 0 to 100 by default; *A specific colonist*,
which adds a list of all the colonists); how far ingredients are looked for
(−/+); a check box per ingredient when the recipe has a choice; warnings
(nobody knows the recipe, the fire is out, nobody fits the workers chosen).
Drop-down lists too long for the screen scroll with the wheel.

Activities: *Fighting* and *Fleeing* for settlers struck by an animal (a notice
tells the player a settler is under attack), *Attacking* for animals.

A wild animal is selected by a click on it, where the colony sees it (after
the settlers, before what lies on the tile; a click again on it goes on to
what is under it). It is dropped when the animal goes out of sight.

When nothing is selected and no tool is in use, the box shows what lies
under the mouse (the old tile readout).

The summary of a colonist is a table too: age and activity as text on the
right, then the needs and the mood as bars (same tooltips as the Needs tab
and the Mood bar).

Tabs of a colonist (exact values and effects are in tooltips, not in the
rows):

- **Needs**: a table, the name of each need on the left and a bar on the
  right, filled up to the value and colored along a scale (red at 0, white in
  the middle, green at 100; orange to blue in the colorblind palette). The
  tooltip gives the exact value, the state (hungry...), what the need does and
  what it weighs on the mood now. Wetness, the warmth of sleep and the bed
  come under the bars.
- **Health** (`docs/HEALTH.md`): the state in a word (healthy, hurt, unable
  to walk, unconscious, in red once hurt); once hurt, the blood as a bar
  (empty where it kills), the bleeding per hour, the pain, what the body can
  do (each ability in percent, white at 100, red toward 0). The body is
  always listed as a table, even when healthy: a row per part (head, torso,
  arms, legs...) with its injuries, its efficiency in percent (white to red)
  and the blood it loses an hour; the tooltip tells what the part is for and
  each injury on it. Above
  the abilities, each deprivation above 0 (hunger, thirst, hypothermia,
  heatstroke) with its severity and a tooltip; the state reads *Weakened* when
  nothing else is wrong. An
  injury's tooltip says whether it is serious (it bleeds on), clotting or
  healing. A hurt settler's portrait in the colonist bar bears a cross, red
  while it bleeds or is down.
- **Mood**: a bar on top (same scale), a break under way, then a table of
  thoughts: the name on the left, what it weighs on the right (red when
  negative, green when positive). The bar's tooltip gives the exact mood and
  its effects (work, learning, quality); a thought's tooltip says what it is
  about, its original value and how it fades.
- **Character**: a table of every attribute: the name and the position in
  percent of the way to either end (−100 % to +100 %), colored by the position:
  red at the lowest, white at the ordinary, green at the highest. No words:
  the figures say more.
- **Knowledge**: a table, the level on the right from red (0) through white
  (mastery, 50) to green (100). The tooltip gives the work speed, what was
  learned today, fading and the masters in the colony.

Row widgets of windows: `ui_layout_meter()` (label, bar under it),
`ui_layout_bar_row()` (label left, bar right), `ui_layout_pair()` and
`ui_layout_triple()` (columns of text, colored), with
`theme_scale_color()` / `theme_sign_color()` (`game/theme.h`).

### Bottom right

From the bottom up, above the bottom bar:

- **Overlay toggles** (icon buttons with a tooltip): temperature for now.
- **Speed buttons**: pause, ×1, ×2, ×3 (Space pauses).
- **Level slider**, vertical, at the right edge: the current level as a
  handle that can be dragged, small + and − buttons (half the size of the others) at its ends, the number below
  it as "level / levels" (counted from 1, the deepest level), a mark at the
  ground level. Page Up / Page Down and Shift + wheel (down: a level up, as the wheel down zooms out) still work.
- Above, right aligned: **date and hour**, **season**, **outdoor
  temperature**, **weather** (sky, snow cover), and the **alerts**.

### Alerts (right side)

A column of short lines right aligned above the date, the most pressing at
the bottom (`game/alerts_list.h`). Each line is a "!" mark and a sentence with
the number of colonists concerned ("2 colonists starving"); a tooltip
explains it, and a click selects the first colonist concerned and centers the
view on it. They come from a core module (`core/alert.h`, tested) that looks
at the game afresh each frame, so a line goes away by itself when its cause
does:

- colonists whose food, water or rest need is critical (starving, thirsty,
  exhausted);
- the colony's food lasting less than a day;
- colonists breaking down (mood);
- colonists without a bed (sleeping on the ground).

Later: idle colonists, enemies, a fire, no workstation for an order...

### Resources (top left)

What the colony has in stock (in stockpiles, on workstations, being
hauled), by item category (food, resources, tools, weapons), in the free
corner left of the colonist bar (`ScreenLayout.corner_width`):

- A group's header shows its total; a click folds or unfolds it. Groups with
  nothing in stock are left out; with nothing at all, one dim line says so.
- One line per item: an icon (the stack as drawn on the map, on a ground
  colored well so that it reads on the dark panel), the name, the count.
- Tooltips: on an item, where its items lie, their weight and the days of
  food they make; on the food header, the days of food of the whole colony.
- The list stops above what the left side shows below it (the inspector and
  its tab, the work grid, the build menu, the tile readout); when lines do
  not fit, the last one says how many are left out (fold a group).

### Messages

Notices from the game (a knowledge discovered, a roof fallen in, a settler
breaking down) appear centered below the colonist bar, each in a box of its
own, newest at the bottom, for 15 seconds, fading out over the last 2.

### Fog of war

What the colony knows (`docs/VISION.md`), on every level and in the view of
the levels below: unexplored tiles are black, tiles seen before but out of
view now are drawn as last seen under a grey veil, tiles in view as they are.
The tile readout says "Unexplored" or that a tile is out of sight and shown as last seen; the
temperature view only covers tiles in view. Tools refuse unexplored tiles
(the card says why): the colony explores by sending settlers. Wild animals
are only drawn, and can only be selected, where a settler sees them now:
nothing of them is remembered.

### Colony log

A main window docked at the bottom left like the work grid (the Log button,
or J): the last 200 notices, the newest first, each with its date above it.
One line says so when there is nothing to report yet. A colonist's own log
(what they did, said, suffered) will be a tab of their inspector, later.

### Game menu

A centered window: Resume, Controls (the old "?" help, F1), Settings, Quit.
Later: save, load, new game. The game is paused while it is open.

### Settings

Opened from the game menu, which it replaces (closing it, with × or Escape,
goes back to the menu); the game stays paused. One row per setting, a label
and a dropdown, with a dim line under it telling what it does:

- **Interface size**: 75 to 200 % in steps of 25 (the font included).
- **Colors**: standard or colorblind-friendly.
- **Language**: every language found in `assets/lang` (the language key
  still switches to the next one).

A choice applies at once and is saved (`settings.cfg` in the user's data
folder: `~/.local/share/colony-sim/` on Linux). The command line
(`--ui-scale`, `--lang`) wins for the session without changing the file.

### Debug

F4 also has a God mode check box (the first six cheats, up to See everything, and the
character editor, all on, or all off when they were all on), a Character editor
check box (a - and a + button at the right of every need bar of settlers and animals and
of every character and knowledge line of settlers: a tenth of a need, 5 percent of an
attribute, 5 knowledge levels per click; knowledge at level 0 is listed too) and a
Temperature row (- and +: 5 degrees off or on the air's, all along). On the command
line: `--debug character_editor` and `--debug god_mode`.

The debug windows (F4 cheats, F5 items, F6 animals) and the debug readout (F3) have no
buttons: keyboard only. The readout goes to the top right corner. With
vision, F4 gets Reveal map and See everything. Both there, with the option
that veils what the selected settler does not see (what no settler sees when
nobody is selected), on the level shown. F6 has a Female and a
Male button per species of wild animal: each arms a tool that puts one more
of them on every clicked tile.

## Theme, scale and accessibility

- **Every color has a role in the theme** (`game/theme.h`): panel, window,
  button, button lit, text, dim text, border, good, warning, bad... Widgets
  never use literal colors. The colorblind-friendly palette is another set
  of the same roles: what is lit or checked turns blue instead of green
  (against the oranges of warnings), and the temperature view goes from
  blue to orange through grey instead of through green and yellow.
- **Information never relies on color alone**: a mark, an icon, a text or a
  pattern goes with it (a lit button also has a bright border).
- **Every size comes from the theme**, multiplied by the UI scale: margins,
  paddings, bar height, button widths, icon size, and the font size. A
  "large" mode is the interface size setting (125 %, 150 %...). New code never uses
  literal pixel sizes; old panels are converted when they are migrated.

## Migration plan

Each step keeps the game playable and is checked on screen (Xvfb).

1. [x] Design (this document)
2. [x] Foundations: theme (color roles, sizes, scale: `--ui-scale 75..200`), screen zones,
       wrapped tooltips, windows kept above the bottom bar
3. [x] Bottom bar (Build, Work, Menu) and the three-level build menu with its card;
       game menu (controls help, language, quit)
4. [x] Bottom right: speeds, temperature overlay, level slider; date, season,
       temperature and weather
5. [x] Colonist bar
6. [x] Inspector box with empty tabs, then its content: colonist (summary, Needs, Mood,
       Character, Knowledge, Gear, Social), workstation, stockpile, gathering zone, tile;
       the floating inspectors go
7. [x] Work grid as a main window above the bottom bar; messages (the debug readout is
       top right already)
8. [x] Resources list (top left)
9. [x] Settings: UI scale (font included), colors, language, remembered between sessions
10. [x] Alerts (core module, column above the date), colony log (main window)
