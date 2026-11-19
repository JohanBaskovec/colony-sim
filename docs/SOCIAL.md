# Social life

What settlers think of each other, how they get on, and what they tell each
other. Code: `core/social.h` (opinions, families, relationships),
`core/experience.h` (what a settler lived through), `core/conversation.h`
(chats); sentences in `game/journal_panel.c`; the Social tab in `game/hud.c`.
Values marked *tuning* are first guesses.

Settlers have no sex (yet): nothing in this system depends on it, and every
sentence is written so that it needs no gendered pronoun. Romance, couples and
the sexual need come later, with sexuality (`ATTRIBUTES.md` §5).

## 1. Opinions

Each settler has an opinion of each other settler, from **-100 to +100**,
shown to the player as a number. It is not the same both ways: Anna may like
Bo who cannot stand her. An opinion is the sum of:

- the **family tie**, if any (§3): parent or child +35, sibling +30,
  grandparent or grandchild +25 *(tuning)*;
- **first impressions** that last: the other's beauty (±10 at the ends of the
  range) and self-confidence (±4);
- every **memory** the settler keeps of the other (§2).

The Social tab lists the others, best liked first: name (and family tie), the
relationship, the opinion. Its tooltip gives each share ("Parent +35", "Came to
this land together +18", "Pleasant chats +9", "Looks -4"...).

## 2. Memories of each other

A memory is something that happened between two settlers. Like a mood
memory, it weighs its full value for a while (its **plateau**), then fades,
halving every **half-life**, until it is forgotten under half a point. The
same memory again adds up to its limit (`max_stack` times its value) and its
plateau starts over. A bond keeps 8 memories at most; a new one pushes out the
one that weighs least.

| Memory | Value | Plateau | Half-life | Stack | When |
|--------|-------|---------|-----------|-------|------|
| Came to this land together | +18 | 30 days | 60 days | 1 | the starting settlers (×0.7 to ×1.5) |
| Pleasant chats | +1.5 | 2 days | 5 days | 12 | a chat that went well (more with an at-ease speaker); a neutral one counts ×0.4 |
| Awkward conversations | -2 | 1 day | 3 days | 5 | a chat that went badly |
| Explained things to them | +3 | 5 days | 10 days | 5 | the other explained a knowledge |
| Good stories | +2 | 2 days | 5 days | 5 | a tale (a kill, a fight; or a lie believed) |
| Opened up to them | +3 | 3 days | 10 days | 4 | something hard confided (a death, hunger, a freezing night) |
| Compliments | +4 | 3 days | 6 days | 4 | |
| Insults | -7 | 4 days | 8 days | 5 | less for a self-assured settler |
| Lied to them | -10 | 5 days | 15 days | 3 | a lie found out |
| Heard others speak well / ill of them | +2 / -3 | 2–3 days | 5–6 days | 4 | gossip, believed as much as the teller is liked |
| Speaks well of them | +4 | 3 days | 8 days | 3 | learned from someone who passed on gossip (§6) |
| Speaks ill of them behind their back | -8 | 4 days | 10 days | 3 | likewise |
| Arguments | -5 | 2 days | 4 days | 4 | both sides |
| Came to blows | -12 | 5 days | 15 days | 3 | both sides |
| Apologized to them | +5 | 3 days | 10 days | 2 | an apology accepted (§6) |
| Turned down their apology | -3 | 2 days | 5 days | 2 | the one refused remembers it |
| Attacked them | -30 | 30 days | 1 year | 3 | a settler struck the other |
| Killed a friend of theirs | -45 | 2 years | 5 years | 2 | |
| Killed someone of their family | -80 | 20 years | 15 years | 2 | a quarter of it is still there after 50 years |

Brawls (§6) bruise but are not attacks: "Came to blows" is a memory of its
own, on both sides. Nothing yet makes a settler attack another settler in
earnest; "Attacked them" and the killings are ready for when something does
(mental breaks).

## 3. Families

The starting settlers are drawn from one people: some of them are related.
For each pair, by age gap *(tuning)*:

- 17 to 40 years apart: parent and child (22 %), a settler having two parents
  at most;
- at most 12 years apart: siblings (18 %), who share their parents and their
  other siblings;
- 36 years or more: grandparent and grandchild (12 %).

Ties follow from each other where the ages allow: a parent's parents are
grandparents, a parent's other children are siblings. Ties are shown as
"Parent", "Child", "Sibling", "Grandparent", "Grandchild" (what the other is to
the settler). The settlers also remember the **journey** that brought them
here (§2), so every starting settler begins with a good opinion of every other.

Newcomers (later) start with no tie and no memory: nobody knows them.

## 4. Company

A need of its own (`Colonist.company`, 1 plenty, 0 lonely), next to food,
water and rest:

- awake and alone, it falls by 1/30 per hour, times the **sociability**
  factor (×0.5 for the most solitary, ×2 for the most sociable);
- listening to a fireside story, telling one, or working beside a master or an
  apprentice fills it (1/3 per hour); each chat fills it at once (+0.4
  pleasant, +0.35 neutral, +0.2 awkward, +0.05 for the one insulted);
- below 0.25 the settler feels **lonely** (a mood state, -5 growing to -10
  over two days) and chats more readily (×1.5);
- an idle settler about to wander **seeks company** instead with a chance
  that grows as the need runs low: none from 0.9 up, half at 0.575, certain
  once lonely *(tuning)*. If someone it does not dislike is already within
  chatting range (3 tiles), it stays; otherwise it walks over to a settler
  awake on its level within 40 tiles, picked by liking (an acquaintance ×1, a
  friend ×2.5, a close friend ×4) and nearness (half as much 10 tiles away),
  never a rival or an enemy. So a settler with plenty of company roams as
  before, and the ones who miss it gather.

## 5. Experiences

Each settler remembers the last 8 things it lived through that make a story
(`core/experience.h`): an animal it killed, one that wounded it, a death it
saw, a relative or friend it lost, nearly starving, nearly dying of thirst, a
night too cold to sleep. The same again within 12 hours renews it (a hunt of
three hares is one story). Fresh stories are told more often (their appeal
halves every 15 days, never below a fifth).

## 6. Chats

Every 250 ticks, each pair of settlers free to chat within 3 tiles may start
one: twice an hour together, times their sociability, their self-confidence
(×0.85 to ×1.15), how much they like each other (×0.4 for bitter enemies to
×1.6 for close friends), and ×1.5 when one of them is lonely *(tuning)*. The
bolder of the two more often leads. A chat is one of:

- **knowledge** (20 %): each brings up a knowledge and the other learns a
  little (`KNOWLEDGE.md` §1.5). When the leader is 20 levels or more ahead,
  it **explains** it: "Explained to Bo how to knap stone";
- a **story** the leader lived through (§5): "Told Bo about killing a wolf",
  "Told Bo about seeing Cyd die";
- a **lie**: a made-up feat (a cave bear, a mammoth ridden, a wolf strangled
  bare-handed...). Unkind, self-assured settlers lie more. The listener sees
  through it with a chance of 45 %, less for a liar at ease, more for a
  self-assured listener; found out, the liar is thought less of. The liar's
  journal tells the truth, the listener's what it believed;
- a **compliment** (kind settlers, mostly to those they like) on the other's
  looks, skill or anything; or an **insult** (abrasive ones, mostly to those
  they dislike) on the other's looks, laziness, smell when soaked, or
  anything. Both are good or bad memories and moods;
- **gossip** about a third settler the leader thinks very well or ill of (40
  points or more): the listener's opinion of that settler follows a little.
  The listener keeps what it heard (4 tales at most, for 10 days) and may
  **pass it on** to the settler it was about: kind words gladly (weight
  0.25), ill words mostly when it likes that settler better than the gossip
  (0.35, else 0.06). The one it was about then remembers that the gossip
  "speaks ill of them behind their back" (or well), believed as much as the
  one who passes it on is liked, and feels insulted (or complimented): "Told
  Cyd what Anna says about them behind their back" / "Learned from Bo what
  Anna says behind their back";
- an **argument**, between settlers who dislike each other (more for unkind
  ones, weight up to 0.46 for bitter enemies); an insult is also **answered
  back** with a chance of 20 % (5 to 75 %: more for the bold and the unkind,
  less toward someone liked). Both remember it and are upset (-5). An
  argument **comes to blows** with a chance of 10 % (2 to 50 %: more for
  unkind, bold settlers who dislike each other, never when one is down): each
  takes a bruise of 2 to 6 points on a part drawn by coverage, never as far
  as half of what the part can take (a brawl neither maims nor kills), and
  both remember it (-12, and -8 of mood);
- an **apology**, from a settler not unkind (kindness above -0.25) to one it
  wronged (insults, lies found out, arguments, blows: 4 points or more) since
  it last apologized: weight 0.5 × (1 + kindness). Accepted with a chance of
  55 %, more from a kind or fond settler and a smooth talker, less for big
  wrongs (10 to 95 %): half of the wrongs are mended, and it is a good
  memory (+5) and mood (+3). Refused, the one who apologized remembers it
  (-3). Either way, no new apology until a new wrong;
- **small talk** about what is real: the leader's last meal ("wild carrots"),
  the weather, or what it suffers from most (the cold, hunger, loneliness...).

How it went: **pleasant**, **neutral** or **awkward**, from both settlers'
social ease and how much they like each other. Each is a memory of the
other (§2); all but awkward ones are also a good mood memory.

## 7. Company of friends and rivals

Every 250 ticks, a settler awake with a friend (or close friend) within 5
tiles on its level feels **good company** (+3, a mood state); with a rival or
an enemy, **unwelcome company** (-4). Both may be felt at once. Sleeping
settlers feel nothing and count for nobody.

## 8. The journal

Every chat goes in both journals (the Social filter), from each side, with
lively sentences built from the facts: "Had a pleasant chat with Bo about wild
carrots", "Grumbled to Bo about the cold" / "Listened to Anna grumble about the
cold", "Heard Anna tell of killing a stag", "Lied to Bo about riding a
mammoth, but Bo did not buy it", "Was told by Anna they smell like a wet boar",
"Heard Anna speak ill of Cyd". Changes of relationship are noted too ("Became
friends with Bo", "Can no longer stand Cyd", "Made peace with Cyd"), and so
are arguments, brawls (also under the Attacks filter, the bruise under
Wounds) and apologies.

Thoughts that come back day after day (hunger, thirst, tiredness, the
nights, the meals, a warm fire, a good chat, the company of friends or
rivals) go under the **Routine** filter only, so that the Thoughts filter
keeps what stands out; they take 16 of the journal's 64 entries at most, a new
one pushing out the oldest of them rather than older chats or wounds.

## 9. Relationships

From the opinion *(tuning)*: **close friend** from 60, **friend** from 25,
**acquaintance** in between, **rival** from -25 down, **enemy** from -60
down. A relationship changes once the opinion is 3 points past a threshold,
so that it does not flicker. Relationships are brought up to date once an
hour; the starting ones are set without a journal note.

**Grief.** When a settler dies, its family feel "Lost a relative" (-20, for
weeks), its friends "Lost a friend" (-12, for days), the others "A settler
died" as before; family and friends also keep the loss as a story (§5).

## 10. Testing it

- **Debug tools.** With the character editor on (debug menu, or `--debug
  character_editor`), a settler's Social tab puts - and + on each row (the
  settler's opinion of that one, 10 points a click) and, under the list,
  buttons acting with a partner (a click on a row picks it; the best liked
  one otherwise): a chat drawn as usual, or forced as small talk, knowledge,
  a story, a lie, a compliment, an insult, gossip, gossip passed on, an
  argument, a brawl (blows for sure) or an apology, wherever the two are;
  what a chat needs is made up when missing (someone to gossip about, a tale
  to pass on, a story to tell). Other buttons make the two enemies, rivals,
  acquaintances, friends or close friends (both ways), or step through the
  family ties. The debug menu turns everyone against everyone (rivals, one
  pair in three enemies), forgets what the tools did, and counts the chats
  held (arguments, brawls, apologies, gossip passed on). What the tools do
  to opinions is a memory of its own ("Debug adjustment" in the tooltip).
- **Feuds.** `--scenario 4` (and `bench_social ... 4`): the gathering
  scenario with the settlers in two clans, friends within and enemies across,
  half of each clan abrasive and bold, the others kind. The F2 menu has the
  same with eight settlers side by side on a bare world.
- **bench_social** `[colonists] [days] [scenario] [seed] [setup]`: a colony's
  social life day by day (chats, insults, arguments, brawls, gossip passed
  on, apologies, the company kept, relationships), the totals, and who
  thinks what of whom; `setup`: `discord`, `feud`, `abrasive`, `kind`,
  `loners` or `sociable`. `bench_social suite [days] [seed]` runs a line for
  each case (ordinary, idle, feuds, discord, abrasive, kind, loners,
  sociable, feuds and discord among idle settlers, a pair, a crowd of 24).
  Build it with the `headless-release` preset (a suite of 5 days takes
  about 20 seconds).

  What it showed (5 days, seed 42): seeking company makes idle settlers
  keep together (lonely 11 % of the time → 0 %, company 0.65 → 0.84, chats
  per settler and day 0.8 → 1.8), and feuding ones less lonely (40 % → 29 %);
  settlers who see only rivals around stay alone (47 % lonely). In a
  working colony it changes little: workers spread over the gathering zones
  and seldom idle, so that after the first day a settler chats about once a
  day and is lonely a fifth of the time.

## 11. Later

- Romance and couples (with sexuality and the sexual need), double beds.
- Mental breaks that hurt others; brawls that others step in to stop.
- Newcomers, births: families growing in the colony.
- Mood memories of a friend's company when working or eating together.
- Tastes (`ATTRIBUTES.md` §6) as topics of chats.
