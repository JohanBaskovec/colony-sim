# Health

Bodies, injuries, bleeding, pain, death and healing, for settlers and wild
animals alike: a deer and a settler are hurt the same way (`core/health.h`).

Status: steps A (injuries), B (deprivation) and D1 (consciousness, §11) are
implemented; step C (care) and the rest of step D (§11.8) are design. Values marked *(tuning)* are first guesses.

## 1. Principles

- **One system for every being.** A body plan lists the parts of a kind of
  being; settlers and animals share the rules, only the plans and sizes differ.
- **Parts, not a single bar.** Each part has its own hit points; what a being
  can do follows from the state of its parts (legs to walk, arms to use the
  hands, the head to stay conscious and to see).
- **Wounds bleed and hurt.** Bleeding drains the blood; pain, blood loss,
  a hurt head and the deprivations take consciousness, which slows, then
  brings down (§11). Death comes from losing too much blood, a destroyed
  vital part, or all of them together.
- **Slow natural healing only** (step A). Small wounds stop bleeding and heal
  by themselves; serious ones keep bleeding and kill without care (care is
  step C).
- **Continuous.** Abilities fall smoothly with the damage, no threshold but
  the few that are plainly so (unconscious, dead).

## 2. Body plans

Hit points are those of an ordinary adult human; an animal's are those of its
plan times its size *(tuning)*. Coverage is the share of the blows that land
on the part when no part is aimed at.

### 2.1 Human

| Part | Hit points | Coverage | Vital | Role |
|------|------------|----------|-------|------|
| Head | 25 | 8 % | yes | consciousness, sight, eating |
| Torso | 40 | 42 % | yes | — |
| Left arm, right arm | 20 each | 12.5 % each | no | hands, half each |
| Left leg, right leg | 25 each | 12.5 % each | no | walking, half each |

Each part may also carry a **limb**, what the being strikes with
(`docs/COMBAT.md` §2): the arms (*arms*), the head (*head*), the front legs
(*forelegs*), the hind legs (*hindlegs*). A plan lacking a limb cannot make
the attacks made with it.

### 2.2 Four-legged animal (deer, boar)

| Part | Hit points | Coverage | Vital | Role |
|------|------------|----------|-------|------|
| Head | 20 | 8 % | yes | consciousness, sight, eating |
| Neck | 20 | 7 % | yes | — |
| Torso | 45 | 45 % | yes | — |
| Four legs | 20 each | 10 % each | no | walking, a quarter each |

Limbs: the head (tusks), the front legs (hooves striking), the hind legs (kicks).

### 2.2b Canine (wolf, fox)

The same parts, with a longer muzzle (head 10 %, torso 43 %). Limbs: the
head (bite), the front legs (claws), the hind legs.

### 2.3 Hare

| Part | Hit points | Coverage | Vital | Role |
|------|------------|----------|-------|------|
| Head | 20 | 10 % | yes | consciousness, sight, eating |
| Torso | 35 | 50 % | yes | — |
| Front legs | 15 each | 10 % each | no | walking, 20 % each |
| Hind legs | 15 each | 10 % each | no | walking, 30 % each (they jump) |

Sizes *(tuning)*: roe deer ×0.7, red deer ×1.4, wild boar ×1.2, hare ×0.15
(a hare's torso has about 5 hit points).

## 3. Injuries

A body holds up to 16 injuries; a new one of the same kind on the same part
when the list is full adds to the largest of them.

| Kind | Bleeding | Pain | Healing | Comes from |
|------|----------|------|---------|------------|
| Cut | ×1 | ×1 | ×1 | blades, sharp stone, claws |
| Bruise | none | ×0.8 | ×1.5 | blows, small falls |
| Bite | ×0.8 | ×1.2 | ×0.8 | teeth (carnivores) |
| Fracture | none | ×1.5 | ×0.25 | big falls, heavy blows |
| Crush | ×0.3 | ×1.2 | ×0.6 | cave-ins, a falling tree |

- **Damage taken** is the blow times the being's damage factor (the
  Toughness attribute for settlers, ×2 fragile to ×0.5 tough; ×1 for animals).
- A part's **efficiency** is 1 − its damage / its hit points; a fracture caps it
  at 10 % until it has healed (two broken legs: no walking). A destroyed part
  (damage at its hit points) is useless; a destroyed vital part kills.
- **Severity** of an injury: its damage over the part's hit points. An injury
  is **serious** from 40 % *(tuning)*.

## 4. Bleeding and blood

- Blood goes from 100 % (full) down; **death at 50 %** *(tuning)*.
- Each injury bleeds its damage × its kind's bleeding × **0.4 % of the blood
  per hour** *(tuning)*: a 10-point cut loses 4 % an hour.
- **Clotting:** an injury that is not serious bleeds less and less and stops
  within 3 hours. A serious one keeps bleeding (until care, step C): a cut of
  16 points on the torso kills in about 8 hours.
- Blood comes back by **10 % a day** once the bleeding has stopped *(tuning)*.

## 5. Pain

- Pain is the sum of the injuries' damage × their kind's pain, over 50 points
  for a human (the body's size for an animal), times the being's pain factor
  (the Pain tolerance attribute for settlers, ×2 to ×0.5), capped at 100 %.
- Pain acts only through consciousness (§11): from about 80 % pain the being
  collapses; pain alone never knocks out nor kills.

## 6. Abilities

Each from 0 to 100 %:

| Ability | From |
|---------|------|
| Consciousness | 100 % less what every source takes (§11) |
| Walking | the legs, each by its share and efficiency; times the vigor (§11.4); 0 when collapsed |
| Hands | the arms, likewise |
| Limb (arms, head, forelegs, hindlegs) | the mean efficiency of the parts that carry it, times the vigor; 0 when collapsed. Below 15 % the attacks made with it cannot be made *(tuning)* |
| Sight | the head's efficiency, times the vigor; 0 when unconscious |
| Eating | the head (−50 % at a destroyed head); 0 when unconscious |

- **Collapsed** (under 50 % consciousness) and **unconscious** (under 25 %):
  §11.3.
- Below 15 % of the legs, the being cannot walk *(tuning)*: it lies where it
  is, awake.
- A settler down (collapsed, unconscious or unable to walk) drops what it carried and
  gives up its task; it cannot be sent anywhere and gets up by itself once
  able to. Walking slows the walk; the hands slow all work and lower the
  weight carried without slowing down.

## 7. Healing (natural)

- An injury heals once it no longer bleeds: **3 points a day** times its
  kind's healing *(tuning)*, the body's size for an animal; gone at 0.
- A small cut (5 points) heals in under two days; a fracture of 15 points in
  about 20 days; a serious cut never stops bleeding without care.

## 8. Death

- A body is left on its tile as an item: a settler's body, an animal's
  carcass (one per species). A body is linked to the one it was: a record of
  them (a settler's name, age, character, knowledge, health and journal as
  they were; an animal's species, sex, character, health and journal) is
  kept, and the body's window shows whose it is, how they died and how long
  ago, with the Character and Journal tabs of the dead, and the Knowledge tab
  of a settler (the other tabs only say they are dead). The record outlives
  the body. It rots like food (rot takes hit points); it is
  not food for settlers. Animals that eat it take weight off it, not hit points. Later it is
  buried, or butchered for meat, hide and bones.
- A settler's death frees the bed, the orders and the selection, raises a
  notice (bled to death, or died of its injuries), leaves a memory on every
  other settler (−8, up to three deaths add up) and a worse one on those who
  saw it (−10 more; without vision, those within 12 tiles on the level).
- A dead herd leader is replaced.
- When every settler is dead, the game pauses and says so, in a notice and a
  line that stays on screen; the world can still be watched (its screen comes
  later).

## 9. Causes of injury (step A)

- A debug tool to hurt a settler or an animal: a blow of 6 to 16 points
  (times the size) of any kind on a part drawn by coverage.
- Falls of a settler (a tree felled under it, the ground taken away): a
  bruise of 3 to 8 points per level fallen, times the levels; from two levels
  a leg also breaks (3 to 6 points). Times the size *(tuning)*.
- A gallery caving in on someone: a crush of 15 to 30 points.
- A felled tree: those under the trunk are crushed (10 to 25 points), those
  under the crown bruised (3 to 8); the woodcutter, beside the foot, steps
  back (the first tiles of its fall are spared) *(tuning)*.
- Work accidents (`docs/KNOWLEDGE.md` §2.4) come later.

## 9.1 Seen by the player

- The **Health** tab of the settler window and of the animal window always
  shows (healthy or not) the state in a word (healthy, hurt, unable to walk,
  collapsed, unconscious), the consciousness and the blood as bars, the
  bleeding, the pain, the afflictions at work (§11.5) and what the body can
  do; in a second window to its right, a **table of the body**: one row per part with its injuries (the kinds, or
  *Intact*), its efficiency (how much of it works) and the blood it loses an
  hour. A tooltip on a row says what the part is for (vital, walking, the
  hands, the limb it strikes with) and gives each injury on it: kind, how bad
  (percent of the part) and how it is going (serious, clotting, healing).
- **Blood on the ground** (`core/blood.h`): whoever bleeds leaves drops
  behind it as it walks, on its steps: now and then a small one for a light
  wound, one or two larger ones at every step for a serious one, some drawn
  out along the walk. Standing or lying still, a pool grows under it. Fresh
  blood is bright red and gleams; it dries brown over an hour and a bit and is
  gone after two game hours. None on water, rock or walls. For the eye only
  (nothing reads it yet: tracking, cleaning come later).
- **Journal** (`core/journal.h`): each blow that lands is noted in the journal of the one struck (who, with what, which part, what it caused: bleeding, a bruise, an open wound, a fracture, a crushing injury), shown in its Journal tab (`docs/UI.md`). Wounds nobody dealt (a fall, a cave-in, a felled tree) are noted too, with what happened instead of an author.
- A cross on the portrait of a hurt settler in the colonist bar, red while it
  bleeds or is down; its state in the tooltip.
- A settler down lies across its tile, eyes closed when unconscious; an
  animal down lies as when asleep.
- Bodies are items (category *Bodies*, left out of stockpiles unless the
  player allows them): a settler's body, an animal's carcass by species.

## 10. Deprivation (step B)

Needs and the weather that go unanswered wear a body down. Each of four
**deprivations** (hunger, thirst, cold, heat) is a severity from 0 to 1 that
grows while its cause lasts, recovers once it is gone, and kills at 1
(`health_strain()`). Settlers and animals alike have them.

| Deprivation | Cause | Grows | Recovers | Death |
|-------------|-------|-------|----------|-------|
| Hunger | food at 0 | to death in 96 h | in 48 h | starved |
| Thirst | water at 0 | to death in 40 h | in 10 h | dehydrated |
| Cold | felt temperature under 0 °C (5 °C lower with cold tolerance at +1) | pressure over 15 °C below it, to death in 14 h at full pressure | in 6 h | froze |
| Heat | felt temperature over 36 °C (heat tolerance moves it) | pressure over 10 °C above it, to death in 10 h at full pressure | in 6 h | heatstroke |

*(tuning)*. Food empties 36 h after a meal and water 24 h, so a settler
without food is weak after a day beyond that, down on the third day and dead
on the fifth; without water, dead on the third. Hunger and thirst count only at
exactly 0; the weather counts every 25 ticks, with the temperature the settler
feels (`COLONIST_STRAIN_TICKS`, wet or by a fire as for sleep).

- **Animals** feel the temperature of the tile they stand on, against limits of
  their species *(tuning)*: they freeze under −10 °C (roe deer, hare), −12 °C
  (boar), −15 °C (red deer), −22 °C (fox), −25 °C (wolf), and overheat over 33
  to 36 °C. The pressure grows over 15 °C beyond the cold limit and 10 °C
  beyond the heat limit, like a settler's. An ordinary winter (−2 °C, −7 °C
  before dawn) hurts none of them; a deep cold snap (−15 °C) hurts the
  smaller ones.
- **Shelter.** An animal within 5 °C of freezing (4 °C of overheating) where it
  stands looks for a tile at least 4 °C warmer (cooler) within reach (a cave, a
  tunnel, under a roof, beside a fire, the shade) and goes there; once in a
  comfortable spot it does not stroll off into the cold. With nowhere better,
  it waits and looks again later (a growing delay). Cold and heat wake a
  sleeper, who goes looking.
- Each deprivation takes consciousness (§11.2): severity^1.5, so little at
  first and more and more; they add up with each other and with wounds. A
  settler starving alone collapses (`TASK_DOWN`) after about 60 hours at food
  0, is knocked out at about 80 and dies at 96.
- A settler collapsed from hunger or thirst cannot eat or drink: nobody
  feeds it until care exists (step C). A settler in the open in winter
  (mean −2 °C, −7 °C before dawn) freezes within a couple of days without a
  fire or a shelter.
- Deprivations add to wounds: a wounded, starving settler collapses sooner,
  and two deprivations together kill sooner than either alone.
- Exhaustion has no body effect of its own: a settler with no rest left
  falls asleep (`NEED_REST`), as before.
- Seen: the Health tab lists each deprivation above 0 as an affliction
  (§11.5); a notice names the cause of a settler's death.

## 11. Consciousness (step D)

Replaced the *weakness* of step B (the worst of the four deprivations, which
slowed walking and the hands by up to 60 %) and the thresholds of step A
(knocked down at 80 % pain, unconscious below 30 % consciousness, pain
slowing walking and the hands by up to 40 %). Step D1 is implemented: the
sources that existed (§11.2 but exhaustion and concussion), the two levels,
the effects, the Health tab, the alert and the notice. Values are first
guesses *(tuning)*.

### 11.1 One figure for the whole body

**Consciousness** goes from 100 % (normal) down to 0 % (dead). Every source
below takes points off it, and **they add up**: a settler both starving and
freezing is in danger even when neither would be enough alone.

> Consciousness = 100 % − (starvation + dehydration + hypothermia +
> heatstroke + blood loss + head + pain + exhaustion + concussion)

- **Deadly sources** (starvation, dehydration, hypothermia, heatstroke, blood
  loss, head) can bring it to 0 %: death.
- **Sources that never kill** (pain, exhaustion, concussion) can bring a being
  down, even knock it out, but not kill it: while the deadly sources add up to
  less than 100 points, consciousness stays at 5 % at least. A settler worn out
  with a broken leg does not die "of tiredness and pain".
- Each source recovers on its own once its cause is gone, **over hours** (the
  recovery times of §10 for the deprivations): consciousness comes back slowly,
  not at the first meal.
- The cause of a death is the largest deadly source (*froze*, *starved*,
  *bled to death*...).

### 11.2 Sources

| Source | Points taken | Deadly | Notes |
|--------|--------------|--------|-------|
| Starvation | 100 × severity^1.5 (severity of §10) | yes | slow at first, then steep: alone, down after about 60 h at food 0, dead at 96 h |
| Dehydration | likewise | yes | alone, down after about 25 h at water 0, dead at 40 h |
| Hypothermia | likewise | yes | dead in 14 h at full cold pressure |
| Hyperthermia (heatstroke) | likewise | yes | dead in 10 h at full heat pressure |
| Blood loss | 0 down to 85 % blood, then up to 100 at 50 % | yes | alone, down at about 67 % blood; death at 50 % blood as before |
| Head | (1 − the head's efficiency) × 60 | yes | a destroyed head still kills at once (vital part) |
| Pain | pain × 62.5 (80 % pain: 50 points) | no | down at about 80 % pain, as the knockdown of step A did |
| Exhaustion *(D2)* | 0 down to 20 % rest, up to 55 at 0 % | no | at 0 rest the being collapses: it falls asleep where it stands |
| Concussion *(D2)* | a blunt blow (bruise, crush) to the head: about 3 points per damage point, up to 60 | no | new (§11.6); fades over about 4 hours |

Later sources: disease and fever, an infected wound (step C), venom and
poison, alcohol and drugs, painkillers (they lower pain, so consciousness comes
back; an injured leg still slows), anesthesia, old age.

### 11.3 Thresholds

| Consciousness | State | What it does |
|---------------|-------|--------------|
| over 90 % | normal | no effect: small hurts cost nothing |
| 90 % to 50 % | lowered | abilities fall smoothly, ×1 at 90 % to ×0.5 at 50 % (§11.4) |
| under 50 % | **collapsed** | lies on the ground, awake: cannot walk, work nor strike, does not dodge; drops what it carries and gives up its task; still sees (less and less far: 12 tiles just after the fall, down to the tiles touching it; for a settler, `docs/VISION.md`); animals do not fear it (it is helpless, easy prey) |
| under 25 % | **unconscious** | as collapsed, and sees nothing (no viewer, `docs/VISION.md`), cannot eat nor drink |
| 0 % | **dead** | §8 |

- A collapsed being gets up at 55 %, an unconscious one comes to (collapsed) at
  30 %: no falling and getting up again from one tick to the next.
- Two levels because they will mean different things with care (step C): a
  collapsed settler is awake, can be given food and water, can later call for
  help; an unconscious one needs a healer.
- *Unable to walk* (both legs broken, §6) stays a state of its own: it comes
  from the legs, not from consciousness.

### 11.4 What consciousness lowers

One factor for the whole body, from consciousness (×1 over 90 %, ×0.5 at
50 %), on:

- walking speed;
- the speed of every work and the weight carried (through the hands);
- the quality of what is made: one works as if one knew that much less (the
  knowledge level times the vigor, before the mood's offset); later, the risk
  of a work accident (`docs/KNOWLEDGE.md` §2.4);
- combat: accuracy (the skill times the vigor, `combat_aim()`), the power of
  blows and the dodge (through the limbs and walking, `docs/COMBAT.md` §3);
- endurance: how long an animal runs, and how fast it recovers (settlers do
  not run yet);
- an animal's alertness: the distances it notices settlers and predators
  from (a weakened prey notices them later);
- the pace of learning a knowledge;
- the range of a collapsed settler's sight (§11.3);
- not yet: talking and mood ("I feel unwell").

**Consciousness and the parts are separate.** Pain acts only through
consciousness: the direct slowdown of up to 40 % (§5) goes, and so does the
weakness's (§10). The parts keep their own, local effect, as in §6: an injured
or broken leg slows walking and injured arms slow work and lower the weight
carried, with or without pain (a leg can be injured and painless, once there
are painkillers).

### 11.5 Afflictions

What touches the whole body, beside the injuries, which touch a part: each with
how bad it is (*mild* under a third, *moderate*, *severe* from two thirds of
its course) and the points it takes.

| Affliction | From |
|------------|------|
| Starvation | hunger (§10) |
| Dehydration | thirst (§10) |
| Hypothermia | cold (§10) |
| Hyperthermia | heat (§10) |
| Exhaustion *(D2)* | rest under 20 % |
| Concussion *(D2)* | a blunt blow to the head |

### 11.6 New injuries

| Kind | Bleeding | Pain | Healing | Comes from |
|------|----------|------|---------|------------|
| Frostbite | none | ×0.6 (numb once deep) | ×0.3 | long exposure to deep cold |
| Burn | ×0.1 | ×1.5 | ×0.5 | fire, and extreme heat |

- **Frostbite** strikes the **extremities** first: arms (the hands), legs (the
  feet), the head (nose, ears); an animal's legs and head. Never the torso
  first. It comes from long exposure under about −10 °C felt (moved by the
  cold tolerance; for animals, from the cold limit of their species); clothes
  and, for animals, fur will protect. Shown by how bad it is: *frostnip*
  (under 15 % of the part), *superficial frostbite* (under 40 %), *deep
  frostbite* (40 % and more, serious). A deep frostbite can destroy the part
  (lost limbs and amputation come with care and later steps).
- **Burns** come mostly from fire: a burning tile, a forest or a building on
  fire, cooking accidents; the air alone burns only over about 55 to 60 °C felt,
  which should stay rare. Shown by degree from how bad it is: *first degree*
  (under 15 %), *second degree* (blisters, under 40 %), *third degree* (40 %
  and more, serious). Infection risk comes with step C.
- Hypothermia and heatstroke are afflictions (the whole body, consciousness);
  frostbite and burns are injuries (a part, its efficiency and pain).

### 11.7 Seen by the player

- The Health tab: a **Consciousness** bar; its tooltip lists each source with
  its points ("Starvation: −12 %, Hypothermia: −9 %, Pain: −7 %") and what it
  does; the afflictions with how bad they are and their points ("Moderate
  (−21 %)"); the state in a word: healthy, hurt, unable to walk, collapsed,
  unconscious, dead.
- An alert counts the settlers in danger (under 70 %); a notice tells when one
  collapses (once each time), another when one dies (with the cause).
- Later: journals of afflictions caught and gone, frostbite and burns
  ("frostbite on the left foot"), collapsing and coming to.
- Animals: the same rules, with their species' tolerances.

### 11.8 Steps

1. Consciousness with the sources that exist (the four deprivations, blood,
   pain, the head), its two levels, its effects; *weakness* removed.
2. Exhaustion and concussion.
3. Frostbite.
4. Burns, once there is fire that spreads.

## 12. Later

- Step C: care by a healer (bandages stop bleeding), infection, disease,
  enforced bed rest.
- Scars (Beauty), lost limbs, prostheses, separate organs, poisoning.
