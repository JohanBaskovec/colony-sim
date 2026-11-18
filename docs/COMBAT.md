# Combat

Blows between beings, for settlers and wild animals alike: a wolf hunting a
deer and a settler fighting a wolf follow the same rules (`core/combat.h`).
Damage, bleeding, pain, knockdown and death are the health module's
(`docs/HEALTH.md`): combat only decides who strikes whom, how often, and
whether the blow lands.

Status: step A is implemented (the module, animals fighting and fleeing,
settlers' self-defense, a debug tool), and the carnivores that use it
(`docs/ROADMAP.md` M5 *Wildlife, step 2*, §6). Values marked *(tuning)* are first
guesses.

## 1. Principles

- **One module, two layers.** The module resolves blows (`combat_blow()`);
  the behaviors above it (animals, settlers) decide who attacks, chases,
  flees or breaks off.
- **Blows are queued, then resolved.** Each tick, attackers adjacent to their
  target push a strike into a `CombatQueue`; the game resolves them after
  everyone has moved (so that nobody's turn depends on who is earlier in the
  slot order), with a random stream of its own.
- **Nothing special for kinds.** A target is a `Combatant` (a settler or an
  animal, by handle); health, dodge and reactions come from what it is.

## 2. Attacks

An attack has a damage in points, an injury kind (`InjuryType`), a base
accuracy, an interval between blows and a reach in tiles (adjacent for now)
*(tuning)*:

| Attack | Injury | Damage | Accuracy | Every | Used by |
|--------|--------|--------|----------|-------|---------|
| Fists | bruise | 3 | 70 % | 1 s | settlers without a weapon |
| Spear thrust | cut | 9 | 80 % | 1.4 s | settlers carrying a thrusting spear |
| Hardened spear thrust | cut | 11 | 80 % | 1.4 s | the same, fire-hardened (×1.2) |
| Kick | bruise | 5 | 50 % | 1.5 s | deer |
| Tusks | cut | 8 | 65 % | 1.4 s | boars |
| Wolf bite | bite | 8 | 70 % | 1.4 s | wolves (step 2) |
| Fox bite | bite | 3 | 70 % | 1 s | foxes (step 2) |

Hares have no attack.

## 3. A blow

1. **Hit chance** = accuracy × the attacker's skill × (1 − ½ × the defender's
   dodge), between 5 % and 95 %. The dodge is the defender's agility (a human
   0.25, a deer 0.35 to 0.45, a boar 0.3, a hare 0.6) times its walking
   ability: someone who cannot walk, or lies knocked out, does not dodge.
2. **Damage** = the attack's damage × the attacker's power × a random 75 % to
   125 %. The part hit is drawn by coverage (`health_blow()`); damage goes
   through the defender's damage factor and size like any injury.
3. A settler's **skill** with a spear comes from its spear hunting knowledge
   (×0.8 at level 0 to ×1.3 at 100); fists are ×1. Its **power** follows its
   strength (×0.64 at 1, ×1 at 10, ×1.4 at 20) and the use of its hands.
   Animals: skill and power ×1.

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
- **Settlers.** A settler struck by an animal drops what it was doing
  (waking if it slept): it fights back, or flees to a tile far from it when
  past its nerve (cornered: it fights). With a thrusting spear in its
  tools it uses it, else its fists. It stops when the animal is dead or far
  (20 tiles); the player's move order ends the fight too.
- A notice tells the player a settler is under attack.

## 5. Seen by the player

- Activities: *Fighting*, *Fleeing* (settlers), *Attacking* (animals).
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
  wolf, a deer many.
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

## 7. Later

- Settlers' mobilization (drafting), hunting orders, ranged weapons
  (throwing spears), line of sight and cover, raids, armor, morale effects of
  seeing a settler hurt, wounded animals fleeing to heal.
