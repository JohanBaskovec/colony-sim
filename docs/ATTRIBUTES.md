# Settler attributes

Settlers differ by what they know (`docs/KNOWLEDGE.md`) and by who they are:
innate **attributes** of the body, the mind and the social self. There are no
all-or-nothing traits: every attribute is a value on a continuous scale, and
most settlers are ordinary in most of them.

Status: step A is implemented (walking, work pace, learning, sleep, appetite, cold and heat tolerance, outlook, temperament), toughness and pain tolerance with health (`docs/HEALTH.md`), courage with combat (`docs/COMBAT.md`), running speed and endurance (the animals', and the settlers' once they can run), and the same attributes for wild animals (§9); the rest is design. Values marked *(tuning)* are first guesses.

## 1. Principles

- **Continuous.** No threshold: a settler a little quicker than most walks a
  little faster, a very quick one much faster. Nothing jumps from ×1 to ×1.5.
- **A bell curve.** The ordinary value is the most common; the further a
  value lies from it, the rarer it is. Extremes are exceedingly rare.
- **Symmetric.** The best and the worst lie as far from the ordinary value:
  factors go from ×0.5 to ×2 (halving and doubling), never from ×0.25 to
  ×1.75.
- **Figures.** The player reads the position in percent (−100 % to +100 %),
  colored from red to green. The tooltip says what the attribute does, in
  words. (The words for the bands, "a little slow", "very quick", are still
  in the code and the translations, `attribute_word_key()`, but nothing shows
  them now.)
- **Independent.** No budget shared between attributes: a settler can be
  gifted in many things or in none (both are rare).
- **Heritable** (with genetics, `GAME_DESIGN.md`): a child gets about the
  average of the parents plus a small mutation, so that selection over
  generations works.

## 2. Values

### 2.1 Position and drawing

- Each attribute is stored as a **position** from −1 to +1, 0 being the
  ordinary settler (in thousandths: an integer, part of `game_hash`).
- Positions are drawn from the game's seeded `Rng` with an approximately normal
  distribution: the sum of 12 uniform draws minus 6 (standard deviation 1),
  times 0.3, clamped to −1..+1 *(tuning)*. This needs no floating point math
  function, so it gives the same result on every machine.
- The game uses the position itself, continuous: many settlers are a few
  percent away from ordinary (×1.03, ×0.95...), a difference that weighs
  little and goes without a word. The position is never rounded towards 0
  (that would bring back a threshold).
- Only the words come in bands. The bands are set on the position, so that
  every attribute has the same shares; for a factor from ×0.5 to ×2 they
  read:

| Word shown | Position | Factor, high side | Factor, low side | Share of settlers |
|------------|----------|-------------------|------------------|-------------------|
| nothing (ordinary) | below 0.14 | below ×1.1 | above ×0.91 | 35 % |
| "a little" | 0.14 to 0.26 | ×1.1 to ×1.2 | ×0.91 to ×0.83 | 25 % |
| "very" | 0.26 to 0.49 | ×1.2 to ×1.4 | ×0.83 to ×0.71 | 29 % |
| "extremely" | 0.49 to 0.77 | ×1.4 to ×1.7 | ×0.71 to ×0.59 | 9 % |
| "exceptionally" | 0.77 and more | ×1.7 to ×2 | ×0.59 to ×0.5 | 1 % |

  (shares with a standard deviation of 0.3, both sides together). Outlook
  (−15 to +15) has its words from about ±2, ±4, ±7 and ±11.5 points.
- With about fifteen attributes on the bell curve, a settler has some ten
  notable ones; four in five are "extremely" something, one in seven
  "exceptionally" something.
- Each band and side has its own translation key ("a little slow", "very
  quick", "exceptionally quick": eight per attribute): words are never built
  from fragments (`docs/I18N.md`).

### 2.2 From a position to an effect

| Kind | Effect at position p | At −1 | At +1 |
|------|----------------------|-------|-------|
| Factor | ×2^p | ×0.5 | ×2 |
| Thought weight (temperament) | ×1.5^p above 0, ×0.6^−p below | ×0.6 | ×1.5 |
| Offset | p × the attribute's range | −range | +range |

Powers are taken with `exp2f()`, like the other continuous effects of the core
(rot, radiant warmth); the stored positions are integers.

## 3. Body

| Attribute | Low side / high side | Effect | Range |
|-----------|----------------------|--------|-------|
| Walking | slow / quick | walking speed | ×0.5 to ×2 |
| Work pace | slow / hard-working | speed of all work, on top of the knowledge level, tools and mood | ×0.5 to ×2 |
| Learning | slow / quick learner | learning rate from every source, on top of age (`KNOWLEDGE.md` §1.8) | ×0.5 to ×2 |
| Sleep | light sleeper needing long nights / sound sleeper | rest regained per hour asleep | ×0.5 to ×2 |
| Appetite | small eater / big eater | how fast the food need falls | ×0.5 to ×2 |
| Cold tolerance | feels the cold / hardy | the coldest felt temperature that is still comfortable (sleep, the cold state) and the one under which the body freezes (hypothermia, `docs/HEALTH.md` §10) | ±5 °C |
| Heat tolerance | feels the heat / at ease in the heat | the warmest comfortable felt temperature (sleep, the hot state) and the one over which the body overheats (heatstroke, `docs/HEALTH.md` §10) | ±5 °C |
| Toughness | fragile / tough | damage taken (with health) | ×2 to ×0.5 |
| Pain tolerance | sensitive / hard to pain | pain felt from injuries, which slows and then knocks down (with health) | ×2 to ×0.5 |
| Running speed | slow / fast | speed when running: an animal's flight and pursuit; a settler's, when settlers can be told to run (later) | ×0.5 to ×2 |
| Endurance | short-winded / enduring | seconds it can run flat out before it is spent, and how fast it recovers (`Animal.fatigue`); a settler's, with the run order (later) | ×0.5 to ×2 |

Age changes some of them later (elders walk and work slower, children eat
less), on top of the innate value.

## 4. Mind

| Attribute | Low side / high side | Effect | Range |
|-----------|----------------------|--------|-------|
| Outlook | gloomy / cheerful | added to the mood (50 + thoughts) | −15 to +15 |
| Temperament | stoic / sensitive | weight of every thought, good and bad | ×0.6 to ×1.5 |
| Sociability | solitary / gregarious | how much company the settler needs (with the social need) | ×0.5 to ×2 |
| Demands | easily pleased / demanding | expectations of comfort and of the colony's wealth (with expectations) | ×0.5 to ×2 |
| Courage | fearful / brave | how hurt a settler must be to flee a fight (`docs/COMBAT.md`) | see below |

**Courage.** In a fight, settlers flee once they are hurt badly enough. A
fearful settler flees at the first wounds, a brave one much later, and the
very bravest never flee *(tuning)*: the settler breaks off once its hurt
fraction (pain, or blood lost; `docs/COMBAT.md` §4) reaches 0.4 when ordinary,
0.08 at −1, 0.75 at +0.5 and 1.1 (never) at +1.

Outlook sets where the mood sits, temperament how far it swings. A cheerful
stoic stays steady and content; a gloomy and sensitive settler breaks down
easily, and is also the happiest of all on a very good day.

## 5. Social

| Attribute | Low side / high side | Effect |
|-----------|----------------------|--------|
| Kindness | abrasive / kind | in chats, kind settlers often pay compliments, abrasive ones insult |
| Social ease | awkward / at ease | how well chats go: how much they raise the other's opinion |
| Self-confidence | insecure / self-assured | how the settler takes others' judgment, and how bold they are |
| Beauty | plain / striking | the first impression and a lasting share of others' opinion |
| Sexuality | see below | who the settler is attracted to, and how strongly |

- **Kindness.** The further from ordinary, the more often a chat turns into a
  compliment (above 0) or an insult (below 0); ordinary settlers seldom do
  either. A compliment gives the other a good memory and raises their opinion
  of the speaker; an insult does the opposite.
- **Social ease** acts on the others: every chat with a settler at ease raises
  the other's opinion more; chats with an awkward one raise it little, or
  lower it.
- **Self-confidence** acts on the settler: insults, rejections and others' bad
  opinion weigh less on a self-assured settler, who also starts chats and
  romances more often (later: leads, persuades, trades). An insecure settler
  is hurt by them and seldom dares. Others are drawn a little to confident
  people.
- **Beauty** sets the opinion others form at first sight, and keeps a share of
  it later; it also counts in attraction. It can change with age and scars
  (with health).
- Kindness, social ease, self-confidence and beauty add up: a striking,
  confident settler who is at ease is liked by everyone, while a plain,
  awkward, insecure one has to win people over slowly.
- **Sexuality** is fluid: one position from "attracted only to the other sex"
  to "attracted only to the same sex", with every shade between. It does not
  follow the bell curve of the other attributes: most settlers lie near the
  first end, some near the other and some between *(tuning)*. It sets the
  chance of attraction to a given settler (with beauty, opinion and
  relationships).
- **Drive**, a second axis of sexuality: how strongly the settler is attracted
  at all, from asexual (no attraction) to a strong drive. Most settlers have
  an ordinary drive, a few a weak one or none *(tuning)*. It also sets how fast
  the sexual need grows (`GAME_DESIGN.md`).

## 6. Tastes (later)

A separate system: each settler likes or dislikes things, also on a continuous
scale and mostly neutral. Liking something gives good thoughts near it or when
using it, disliking it bad ones.

- Drugs and drink (with them), a kind of food, cooked meals.
- Animals, nature, darkness, rain, crowds or solitude.
- Women, men: an opinion bias, separate from attraction (sexuality).
- Kinds of work, which could also suggest work priorities.

## 7. Shown to the player

- Attributes are known from the start: the inspector shows them as soon as
  the settler arrives, nothing is found out over time.
- The Character tab of the inspector is a table of every attribute, grouped
  by body, mind and social: the name on the left, the position in percent on
  the right (−100 % to +100 %); the row is colored by that position, red at the
  lowest, white at the ordinary, green at the highest. A tooltip tells what each
  one does, in words.
- Sexuality is shown in words too ("mostly attracted to men", "asexual").

## 8. Open questions

None at the moment.

Settled so far: attributes are known at once; asexuality is a second axis of
sexuality (drive); courage sets how hurt a settler must be to flee a fight;
cold and heat tolerance shift the comfortable temperatures by up to 5 °C.

## 9. Animals

Wild animals have the same attributes, drawn the same way (`Animal.attributes`,
rolled when the animal is born from a copy of the wildlife stream, so that
drawing them shifts nothing else). A settler is, in the end, one species among
others: the **species** gives the ordinary values (`AnimalDef`: speeds, hours,
limits of temperature, nerve; for settlers, the `COLONIST_*` constants), and
the attributes move them for the individual, with the same factors (×0.5 to ×2,
±5 °C, and so on):

| Attribute | What it changes in an animal |
|-----------|------------------------------|
| Walking | walking speed (`animal_walk_speed()`) |
| Running speed | running speed: flight and pursuit (`animal_run_speed()`); replaces the old "flight speed" |
| Endurance | seconds of sprint (`animal_sprint_seconds()`), and how fast it recovers |
| Toughness, pain tolerance | damage taken and pain, as for settlers (`animal_set_attributes()`) |
| Courage | the hurt fraction at which it breaks off a fight: the species' nerve times the ratio of the settlers' (`animal_nerve()`) |
| Cold, heat tolerance | the temperatures it freezes and overheats at, ±5 °C |
| Appetite, sleep | how fast hunger comes back, how fast rest is regained |

Learning and temperament are fixed at **−100 %** for every animal (it learns
nothing and thoughts do not weigh on it: `attributes_roll_animal()`); work
pace and outlook are drawn like the rest but have no effect on an animal. A
Character tab in the animal's window shows them all, as for settlers.
