# Health

Bodies, injuries, bleeding, pain, death and healing, for settlers and wild
animals alike: a deer and a settler are hurt the same way (`core/health.h`).

Status: step A is implemented; steps B (needs that harm) and C (care) are
design. Values marked *(tuning)* are first guesses.

## 1. Principles

- **One system for every being.** A body plan lists the parts of a kind of
  being; settlers and animals share the rules, only the plans and sizes differ.
- **Parts, not a single bar.** Each part has its own hit points; what a being
  can do follows from the state of its parts (legs to walk, arms to use the
  hands, the head to stay conscious and to see).
- **Wounds bleed and hurt.** Bleeding drains the blood; pain slows, then
  knocks down. Death comes from losing too much blood or from a destroyed
  vital part.
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

### 2.2 Four-legged animal (deer, boar)

| Part | Hit points | Coverage | Vital | Role |
|------|------------|----------|-------|------|
| Head | 20 | 8 % | yes | consciousness, sight, eating |
| Neck | 20 | 7 % | yes | — |
| Torso | 45 | 45 % | yes | — |
| Four legs | 20 each | 10 % each | no | walking, a quarter each |

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
- Walking and the hands are slowed by up to 40 % at full pain.
- **From 80 % pain the being is knocked down** (unconscious) *(tuning)*.

## 6. Abilities

Each from 0 to 100 %:

| Ability | From |
|---------|------|
| Consciousness | the blood (100 % down to 70 % blood, then down to 0 at 50 %) times the head (−60 % at a destroyed head); 0 at full pain (80 %) |
| Walking | the legs, each by its share and efficiency; times the pain slowdown |
| Hands | the arms, likewise |
| Sight | the head's efficiency |
| Eating | the head (−50 % at a destroyed head) |

- Below 30 % consciousness the being is **unconscious**: it lies on the
  ground and sees nothing (a settler is no viewer, `docs/VISION.md`; animals
  do not fear it); walking, the hands, sight and eating are 0.
- Below 15 % walking, the being cannot walk *(tuning)*: it lies where it is.
- A settler down (unconscious or unable to walk) drops what it carried and
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
  carcass (one per species). It rots like food; it is not food. Later it is
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

- The settler window's **Health** tab, and the animal window: the state in a
  word (healthy, hurt, unable to walk, unconscious), the blood as a bar, the
  bleeding, the pain, what the body can do, and each injury (part, kind, how
  bad), with the details in tooltips.
- A cross on the portrait of a hurt settler in the colonist bar, red while it
  bleeds or is down; its state in the tooltip.
- A settler down lies across its tile, eyes closed when unconscious; an
  animal down lies as when asleep.
- Bodies are items (category *Bodies*, left out of stockpiles unless the
  player allows them): a settler's body, an animal's carcass by species.

## 10. Later

- Step B: hunger, thirst, exhaustion, cold and heat that weaken, then kill.
- Step C: care by a healer (bandages stop bleeding), infection, disease,
  enforced bed rest.
- Scars (Beauty), lost limbs, prostheses, separate organs, poisoning.
