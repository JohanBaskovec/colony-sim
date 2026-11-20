# Combat

Blows between beings, for settlers and wild animals alike: a wolf hunting a
deer and a settler fighting a wolf follow the same rules (`core/combat.h`).
Damage, bleeding, pain, knockdown and death are the health module's
(`docs/HEALTH.md`): combat only decides who strikes whom, how often, and
whether the blow lands.

Status: step A is implemented (the module, animals fighting and fleeing,
settlers' self-defense, a debug tool), and the carnivores that use it
(`docs/ROADMAP.md` M5 *Wildlife, step 2*, §6). Weapons (§7) are implemented
(step 1 of §9: `core/weapon.h`, the primary weapon), and so is the
mobilization (step 2: the state, its end, the button, key and marks) and
wielding (step 3: the weapon in hand, drawing, the *Change weapon* list);
the *Attack* order (step 4) is designed, not implemented. Values marked
*(tuning)* are first guesses.

## 1. Principles

- **One module, two layers.** The module resolves blows (`combat_blow()`);
  the behaviors above it (animals, settlers) decide who attacks, chases,
  flees or breaks off.
- **Blows are queued, then resolved.** Each tick, attackers in reach of their
  target push a strike into a `CombatQueue`; the game resolves them after
  everyone has moved (so that nobody's turn depends on who is earlier in the
  slot order), with a random stream of its own.
- **Nothing special for kinds.** A target is a `Combatant` (a settler or an
  animal, by handle); health, dodge and reactions come from what it is.

## 2. Attacks

An attack has a damage in points, an injury kind (`InjuryType`), a base
accuracy, an interval between blows, a reach in tiles (adjacent for now: one
of the eight tiles around, on the same level, and not diagonally across a
wall's corner, as for a step: `combat_can_strike()`) and
the **limb** it is made with (`BodyLimb`, `docs/HEALTH.md` §2) *(tuning)*:

| Attack | Limb | Injury | Damage | Accuracy | Every | Used by |
|--------|------|--------|--------|----------|-------|---------|
| Fists | arms | bruise | 3 | 70 % | 1 s | settlers without a weapon |
| Spear thrust | arms | cut | 9 | 80 % | 1.4 s | settlers with a thrusting spear |
| Hardened spear thrust | arms | cut | 11 | 80 % | 1.4 s | the same, fire-hardened (×1.2) |
| Weapons of §7 | arms | | | | | settlers with that weapon |
| Kick | hind legs | bruise | 5 | 50 % | 1.5 s | deer |
| Hoof strike | front legs | bruise | 4 | 50 % | 1.5 s | deer |
| Tusks | head | cut | 8 | 65 % | 1.4 s | boars |
| Wolf bite | head | bite | 8 | 70 % | 1.4 s | wolves |
| Wolf claws | front legs | cut | 4 | 60 % | 1.2 s | wolves |
| Fox bite | head | bite | 3 | 70 % | 1 s | foxes |
| Fox claws | front legs | cut | 1.5 | 60 % | 1 s | foxes |

Hares have no attack.

**Several attacks, and the body.** A species lists its attacks, each with a
weight (a wolf bites three times for each time it claws: weights 3 and 1).
For each blow the animal draws among the attacks it can make, by weight times
the ability of their limb (`health_limb_ability()`): a limb counts when it is
at least 15 % whole; a damaged limb is used less and strikes with its ability
as the blow's power. A wolf with ruined forepaws only bites; a deer that lost
its hind legs still strikes with its hooves; one that lost both cannot
fight and runs. Nothing is drawn when only one attack can be made. A settler
holds everything it strikes with in its hands: with both arms lost or broken
(below 15 %) it cannot strike at all, with a spear or without, and runs (or,
cornered, takes it).

## 3. A blow

1. **Hit chance** = accuracy × the attacker's skill × its vigor (a dazed
   attacker aims worse, `docs/HEALTH.md` §11.4) × (1 − ½ × the defender's
   dodge), between 5 % and 95 %. The dodge is the defender's agility (a human
   0.25, a deer 0.35 to 0.45, a boar 0.3, a hare 0.6) times its walking
   ability: someone who cannot walk, or lies collapsed, does not dodge.
   A miss is a **dodge** when the same roll would have hit without the dodge
   (`BlowResult.dodged`, for the journals: no draw of its own).
2. **Damage** = the attack's damage × the attacker's power × a random 75 % to
   125 %. The part hit is drawn by coverage (`health_blow()`); damage goes
   through the defender's damage factor and size like any injury.
3. A settler's **skill** with a spear comes from its spear hunting knowledge
   (×0.8 at level 0 to ×1.3 at 100); fists are ×1. Its **power** follows its
   strength (×0.64 at 1, ×1 at 10, ×1.4 at 20) and the use of its hands.
   Animals: skill ×1, power the ability of the limb used (×1 when it is whole).

## 4. Fighting and breaking off

- A being hurt enough breaks off: its **hurt fraction** (the larger of its
  pain and of the share of its blood it can still lose before it dies) reaches
  its *nerve*. An animal's nerve is its species' (boar 0.7, deer 0.2, hare
  never fights) times its courage's ratio to an ordinary settler's; a settler's comes from the **Courage** attribute: 0.4 for an
  ordinary settler, 0.08 for the most fearful, 1.1 (never) for the very
  bravest (position +0.86 and more).
- **Animals.** A struck animal fights back if its species does (the boar) and
  is not past its nerve, else runs from its attacker (and its herd with it).
  An attacker chases its target (at its running speed) and strikes when
  adjacent, until the target is dead, gone far away or the attacker breaks
  off; it ignores settlers' presence meanwhile.
- **Around the target.** Nobody stops on another's tile
  (`docs/ARCHITECTURE.md`, *Occupancy*): attackers head for free tiles around
  their target, each its own, never the target's. When there is no room left
  (a dead end, a pack around a deer), the others wait and look again every
  half second; an animal gives up after a few tries (and may go for another
  prey), a settler waits. Those down keep their tile.
- **Settlers.** A settler struck by an animal drops what it was doing
  (waking if it slept): it fights back, or flees to a tile far from it when
  past its nerve (cornered: it fights). It draws its weapon
  (`colonist_weapon()`: its primary weapon, else the best it keeps, among
  those its arms and hands can wield; §7 *Wielded*), else fights with its
  fists, and puts it back when the fight ends. It stops when the animal is dead or far
  (20 tiles); the player's move order ends the fight too.
- A notice tells the player a settler is under attack.

## 5. Seen by the player

- Activities: *Fighting*, *Fleeing* (settlers), *Attacking* (animals).
- **Blows on screen.** The attacker faces its enemy, draws back a little
  (0.2 tile, rearing, 0.3 s), then lunges (0.32 tile past its place, head
  down, 0.12 s) and goes back (0.5 s). When the blow lands, the one struck
  flinches back, flashes, and shows the attack's mark for under a second:
  three white claw scratches (claws), two toothed jaws closing and leaving
  red punctures (bites), a slash red at its heart with a few drops (spear
  thrusts, tusks), a burst of short rays (fists, kicks, hooves). A miss is a
  lunge alone. The simulation resolves the blow at once; the screen plays it
  over the next 1.2 s, at the game's speed (`core/strike_anim.h`).
  `tools/preview_attacks.py` makes a GIF of every mark with the game's sprites.
- A debug tool (F4 menu): click the attacker, then its target (a settler or an
  animal), to make one attack the other.

## 6. Carnivores (wildlife, step 2)

- **Wolf**: packs of three to five (males in groups of one or two), hunts roe
  deer, red deer and hares; bites (`ATTACK_WOLF_BITE`); fights back, breaks
  off at 55 % hurt; sprints for 30 s; hunts settlers when desperate. **Fox**:
  alone, hunts hares; bites lightly, breaks off at 35 %, sprints for 14 s.
  Neither eats plants: they eat carcasses only (an animal's, never a settler's
  body).
- **Hunger.** Wolves go 60 hours from full to empty, foxes 36. Below half they
  hunt; a carcass nearby comes first. A bite takes 0.6 kg (wolf) or 0.15 kg
  (fox) and fills that over the stomach (5 kg, 1 kg): a hare is a few bites for a
  wolf, a deer many. A bite takes weight off the carcass, not hit points (those
  are what rot takes): it stays as sound as it was, only lighter, and is gone
  with the last bite.
- **Sharing.** One eats on the carcass's tile, the others around it (a tile
  beside it with a direct step to it: not across a wall's corner), each on a
  tile of its own; plant eaters do the same with a patch of food.
- **The hunt.** The hungry predator picks the nearest prey it sees within its
  range (30 tiles, 20 for a fox) and runs at it; its pack joins. A prey that
  sees a predator runs, its herd with it. Running tires: a spent animal runs
  at a third of its speed (a hare after 8 s, a deer 18 to 22 s, a wolf 30 s),
  which is how packs catch deer; it recovers over a minute.
- **Settlers as prey.** A wolf hungrier than 30 % also considers settlers it
  sees (counted twice as far as an animal) and stops fearing them; a fed wolf
  runs from settlers like any animal. The settler defends itself (§4).
- Not yet: births (the population only shrinks: *Wildlife, step 3*), tracks,
  hunting orders, young, bears and lynxes.

## 7. Weapons (designed)

Many things a settler carries can strike: a stone in the fist, a sharp flake,
a hand axe, a branch as a club, and of course spears. What is soft, light or
growing cannot (twigs, grass, every plant and food, moss, reeds, bodies),
nor what is too heavy to swing (a stone block). Each weapon item has an
attack (§2) and the hands it takes *(tuning)*:

| Item | Attack | Injury | Damage | Accuracy | Every | Hands |
|------|--------|--------|--------|----------|-------|-------|
| (none) | fists | bruise | 3 | 70 % | 1 s | — |
| Cobble, flint | stone in the fist | bruise | 5 | 70 % | 1.1 s | 1 |
| Sharp flake | slash | cut | 4 | 75 % | 1 s | 1 |
| Chopper | chop | cut | 5 | 70 % | 1.2 s | 1 |
| Hand axe | chop | cut | 7 | 75 % | 1.2 s | 1 |
| Branches | club | bruise | 5 | 65 % | 1.3 s | 1 |
| Wood (a log) | heavy club | bruise | 8 | 55 % | 1.8 s | 2 |
| Digging stick | staff | bruise | 5 | 75 % | 1.2 s | 2 |
| Throwing spear | thrust (melee) | cut | 7 | 75 % | 1.3 s | 1 |
| Thrusting spear | thrust | cut | 9 | 80 % | 1.4 s | 2 |

Fire-hardened versions strike ×1.2. Skill: the spear hunting knowledge for
spears (§3), ×1 for the rest until a fighting knowledge exists. Throwing
spears are thrown later (ranged weapons). A weapon's **worth**, to pick the
best, is its damage × accuracy ÷ interval, times its quality factor and its
condition; on a tie, the better quality, then the less worn.

- **Weapons kept.** A settler fights with the weapons it keeps (its tools
  and the items locked on it), not with those of a load it hauls (the load
  is put down when it wields).
- **Primary weapon.** A button with two crossed blades on each weapon's row
  of the Gear tab (`docs/CARRYING.md` §7.3) makes it the settler's primary
  weapon: one per settler, that very item. It also locks it *kept on the
  settler* (else it could be set down); taking the mark away leaves the lock.
  A primary weapon put down or used up is no longer primary.
- **Wielded.** A weapon ready to strike is in the hands it takes (two: both
  hands). Wielding is a state of its own, apart from the player's locks:
  when it ends, the weapon goes back to where it fits best and its lock is
  what it was. Wielding drops what fills the hands it needs (the load is put
  down, not forbidden; free kept items that no longer fit are set down).
  **Drawing** takes time by where the weapon was: none from a hand, half a
  second from under an arm or the shoulders, two seconds slung *(tuning)*.

Implemented (step 3): `Colonist.wielding` and `wielded` (a slot of its
tools, -1 for its fists), `colonist_wielded()`. The arrangement
(`core/carry.h`) places the weapon wielded first: a one-handed weapon in the
better arm's hand, a two-handed one held with both hands
(`carry_arrange_gripped()`, `CARRY_HOLD_GRIP`), which leaves the places under
the arms free (unlike an armful) but no hand to steady the shoulders. A
weapon can be wielded (`colonist_can_wield()`) when the arms can hold it and
no item kept in hand takes the hands it needs; other locked items that find
no place while it is wielded stay on the settler (the Gear tab lists them
under *No place*), free ones are set down. The first blow waits the drawing
time (the attack cooldown): `COLONIST_DRAW_TICKS` (half a second) from under
an arm or the shoulders, `COLONIST_DRAW_SLUNG_TICKS` slung, none from a
hand, `COLONIST_DRAW_GROUND_TICKS` (one second) for one taken up from the
ground. A weapon wielded whose arms fail (an arm broken) leaves the settler
its fists.

## 8. Mobilization (step 2 implemented)

The player **mobilizes** (drafts) settlers to fight, and **demobilizes**
them. A button among the settler's actions (and a key, a physical position
as every control), for every settler selected: *Mobilize*, or *Demobilize*
when all are mobilized.

- **Who.** Not a settler in a mental break, fleeing, knocked out or dead.
  One asleep wakes up and gets up.
- **Its weapon.** It wields at once: its only weapon if it carries one; with
  several, its primary weapon, else the best one (§7); with none, its
  fists.
- **Change weapon.** While one settler is selected and mobilized, a *Change
  weapon* button opens a list upward (the button is at the bottom of the
  screen): its fists, the weapons it carries, and the weapons lying within
  one tile of it (on its tile or a neighbor). Picking one wields it (drawing
  time); one from the ground is taken first (it becomes a kept item,
  locked), and the weapon it held goes back to its place, or is set down
  when it fits nowhere.
- **Needs.** A mobilized settler ignores its needs: they keep falling, but
  it does nothing for them; their effects on health stay (hunger, exhaustion
  up to collapse). It does not fall asleep while it stands.
- **Orders.** It stays where it is put and strikes any enemy in reach,
  without chasing. The orders: *Move* (as today) and *Attack* (an action
  that arms a tool, then a click on a target: it walks to it and strikes
  until it is dead, gone or the order changes). It no longer flees from
  fear; it still flees when it cannot fight any more (no usable arm),
  which demobilizes it.
- **The end.** It is demobilized by the player, or when it breaks
  (mental break), flees, is knocked out or dies (a notice says why). Its
  weapon is then no longer wielded (§7), and it goes back to its life.
- **Seen.** A mark on the settler on the map and in the colonist bar; the
  activity *Mobilized* (with what it does: moving, attacking). The Gear tab
  says which item is in hand to fight.
- **Self-defense** (not mobilized) follows the same choice of weapon: the
  primary, else the best one carried, with the drawing time.

Implemented (step 2): `Colonist.drafted`, `colonist_can_draft()` and
`colonist_set_drafted()`, `game_set_drafted()` for a selection; the action
button (a round shield before a spear, lit when all are mobilized) for one
or several settlers, the key `APP_KEY_DRAFT` (B on QWERTY), the shield over
the settler's head on the map and on its portrait in the colonist bar, the
activity *Mobilized* (*moving*, *fighting*) and the notices of its end.
Step 3: mobilized, it wields `colonist_weapon()` at once; the *Change
weapon* action (a blade between two arrows), for one mobilized settler,
opens its list upward (`ui_popup_list()`): its fists, the weapons it keeps
and can wield, and those lying on an explored tile within a tile of it (one
line for items alike), `game_weapon_choices()`; the one picked is wielded
(`game_wield()`; one from the ground is taken up, kept and locked:
`colonist_wield_item()`). Until step 4, a mobilized settler struck by an animal still walks up to it
and fights as in self-defense (without fleeing from fear), and it does not
yet strike an enemy that merely comes in reach.

## 9. Steps of weapons and mobilization

1. *(done)* The weapons (§7): an attack for each weapon item, the hands it takes,
  its worth; the primary weapon and its button; self-defense with the best
  weapon carried.
2. *(done)* Mobilization (§8): the state, needs ignored, waking up, the end
  conditions and their notices, the button and key, the marks on screen.
3. *(done)* Wielding: the weapon in hand at mobilization, the drawing time, the
  *Change weapon* list (with the weapons lying within a tile).
4. The *Attack* order, and striking enemies in reach while mobilized.

## 10. Later

- Hunting orders, ranged weapons (throwing spears, stones), line of sight
  and cover, raids, armor, a fighting knowledge, morale effects of seeing a
  settler hurt or of a long mobilization, mobilized settlers chasing the
  enemies they see on their own, wounded animals fleeing to heal.
