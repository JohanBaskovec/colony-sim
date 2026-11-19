/*
 * The journal of a settler or an animal: what happened to it, newest last in
 * a small ring (the oldest are forgotten). Four kinds of entries, which the
 * player may filter: new thoughts (good and bad), wounds (who struck it, with
 * what, where, and what it caused), attacks (the blows it gave, and those it
 * took, wounding it or not) and social life (chats). A wound from a blow is
 * both a wound and an attack.
 *
 * Entries hold facts, never text: the frontend writes the sentences, in the
 * player's language. Names of others (proper nouns) are copied, so that the
 * entry outlives who it is about. For the eye only: nothing in the
 * simulation reads a journal and game_hash() leaves it out.
 */
#ifndef COLONY_CORE_JOURNAL_H
#define COLONY_CORE_JOURNAL_H

#include "core/health.h"
#include "core/knowledge.h"
#include "core/mood.h"

#include <stdbool.h>
#include <stdint.h>

#define JOURNAL_MAX 64
/* Routine thoughts (thought_is_routine()) take this many entries at most: a
 * new one pushes out the oldest of them, not what stands out. */
#define JOURNAL_ROUTINE_MAX 16
#define JOURNAL_NAME_SIZE 32

typedef enum JournalKind {
    JOURNAL_THOUGHTS,
    JOURNAL_WOUNDS,
    JOURNAL_ATTACKS,
    JOURNAL_SOCIAL,
    JOURNAL_ROUTINE, /* Thoughts that come back day after day (thought_is_routine()). */
    JOURNAL_KIND_COUNT
} JournalKind;

/* Which kinds are shown: a bit per JournalKind. */
typedef uint8_t JournalFilter;
#define JOURNAL_BIT(kind) ((JournalFilter)(1u << (unsigned)(kind)))
#define JOURNAL_ALL ((JournalFilter)((1u << JOURNAL_KIND_COUNT) - 1u))

typedef enum JournalEvent {
    JOURNAL_THOUGHT, /* `what`: a ThoughtType. */
    JOURNAL_WOUND,   /* `what`: an AttackType; plan, part, injury; the attacker. */
    JOURNAL_HURT,    /* `what`: a HurtCause; plan, part, injury (no author). */
    /* A chat with `other`: `topic` a ChatTopic, `what` its subject (a
     * knowledge, a food item, a WeatherType or JOURNAL_WEATHER_SNOW, a
     * ThoughtType complained of), `outcome` a ChatQuality. */
    JOURNAL_CHAT,
    /* A blow it gave: `what` an AttackType, `outcome`; on a hit, the plan,
     * part and injury of who it struck, named by `species`/`other`. */
    JOURNAL_STRIKE,
    /* A blow it took that did not wound it (a wound is JOURNAL_WOUND): `what`
     * an AttackType, `outcome`; the attacker; on a hit, the plan and part. */
    JOURNAL_STRUCK,
    /* Social life with `other` (docs/SOCIAL.md). Stories: `topic` a
     * StoryKind, `what` an ExperienceType or a LieType, the animal of it in
     * `animal` (species + 1) and `animal_sex`, the settler of it in
     * `subject`. */
    JOURNAL_EXPLAINED,    /* Explained the knowledge `what` to `other`. */
    JOURNAL_LEARNED,      /* `other` explained the knowledge `what`. */
    JOURNAL_TOLD_STORY,   /* Told `other` a true story. */
    JOURNAL_HEARD_STORY,  /* Heard `other` tell a story (true or made up: the
                             listener does not know which). */
    JOURNAL_LIED,         /* Told `other` a made-up story; `outcome` 1: found out. */
    JOURNAL_CAUGHT_LIE,   /* Did not believe `other`'s made-up story. */
    JOURNAL_COMPLIMENT,   /* Paid `other` a compliment: `what` a ComplimentType,
                             the knowledge praised in `topic`. */
    JOURNAL_COMPLIMENTED, /* Was paid one by `other` (likewise). */
    JOURNAL_INSULT,       /* Insulted `other`: `what` an InsultType. */
    JOURNAL_INSULTED,     /* Was insulted by `other`. */
    JOURNAL_GOSSIP,       /* Spoke of `subject` to `other`: `outcome` 1 well, 0 ill. */
    JOURNAL_HEARD_GOSSIP, /* Heard `other` speak of `subject` (likewise). */
    JOURNAL_REPORTED,     /* Told `other` what `subject` says of them: `outcome` 1 well. */
    JOURNAL_HEARD_REPORT, /* Learned from `other` what `subject` says of it (likewise). */
    JOURNAL_ARGUED,       /* Argued with `other`. */
    JOURNAL_BRAWL,        /* Came to blows with `other`. */
    JOURNAL_APOLOGIZED,   /* Apologized to `other`: `outcome` 1 forgiven. */
    JOURNAL_FORGAVE,      /* `other` apologized: `outcome` 1 forgave, 0 refused. */
    JOURNAL_RELATION,     /* The relationship with `other` became `what`
                             (a RelationLevel), from `outcome`. */
    JOURNAL_EVENT_COUNT
} JournalEvent;

/* How a blow went. */
typedef enum JournalBlow {
    JOURNAL_BLOW_MISSED,
    JOURNAL_BLOW_DODGED,   /* It would have hit, but for the dodge. */
    JOURNAL_BLOW_HARMLESS, /* It hit, and did no harm. */
    JOURNAL_BLOW_WOUNDED,
    JOURNAL_BLOW_KILLED, /* The blow killed. */
    JOURNAL_BLOW_COUNT
} JournalBlow;

/* What a chat was about (JOURNAL_CHAT). */
typedef enum ChatTopic {
    CHAT_TOPIC_NONE,      /* Small talk. */
    CHAT_TOPIC_KNOWLEDGE, /* `what`: a KnowledgeType. */
    CHAT_TOPIC_FOOD,      /* `what`: the ItemType of a recent meal. */
    CHAT_TOPIC_WEATHER,   /* `what`: a WeatherType, or JOURNAL_WEATHER_SNOW. */
    CHAT_TOPIC_COMPLAINT, /* `what`: a ThoughtType the speaker suffers from. */
    CHAT_TOPIC_COUNT
} ChatTopic;
#define JOURNAL_WEATHER_SNOW 5

/* How a chat went. */
typedef enum ChatQuality {
    CHAT_NEUTRAL,
    CHAT_PLEASANT,
    CHAT_AWKWARD,
    CHAT_QUALITY_COUNT
} ChatQuality;

/* A story told: lived through (an ExperienceType) or made up (a LieType). */
typedef enum StoryKind { STORY_TRUE, STORY_LIE } StoryKind;

/* The same miss again soon after the last entry is counted in it
 * (JournalEntry.repeats) instead of filling the journal: within this many ticks. */
#define JOURNAL_REPEAT_TICKS (10 * 60)

typedef struct JournalEntry {
    uint64_t tick;
    uint8_t event;  /* JournalEvent */
    uint8_t plan;   /* Wounds: BodyPlan of who was struck... */
    uint8_t part;   /* ...the part, in that plan... */
    uint8_t injury; /* ...and what the blow caused (InjuryType). */
    uint16_t what;
    /* Who the entry is about: an animal's species + 1 (the wounds it dealt), or
     * 0 for a settler, then named in `other`; an animal's sex (AnimalSex). */
    uint8_t species;
    uint8_t sex;
    uint8_t outcome; /* Blows: a JournalBlow. */
    uint8_t repeats; /* Times the same happened again since (see JOURNAL_REPEAT_TICKS). */
    /* Social life: what a chat or a story was about, the animal of a story
     * (species + 1, 0 for none) and its sex, and a third settler named. */
    uint8_t topic;
    uint8_t animal;
    uint8_t animal_sex;
    bool heard; /* A chat: the other brought the topic up. */
    char other[JOURNAL_NAME_SIZE];
    char subject[JOURNAL_NAME_SIZE];
} JournalEntry;

typedef struct Journal {
    JournalEntry list[JOURNAL_MAX];
    int next;  /* Where the next entry goes. */
    int count; /* Up to JOURNAL_MAX. */
} Journal;

/* Someone an entry names: a settler (`name`, `species` 0) or an animal
 * (`species` its AnimalSpecies + 1, and its AnimalSex). */
typedef struct JournalParty {
    const char *name;
    int species;
    int sex;
} JournalParty;

/* The kinds an event is filed under (a bit per JournalKind; 0 for an invalid
 * one): it is shown when one of them is. */
JournalFilter journal_event_kinds(JournalEvent event);

/* The kinds an entry is filed under: its event's, but a routine thought
 * (thought_is_routine()) is routine only. */
JournalFilter journal_entry_kinds(const JournalEntry *entry);

/* Whether a thought is a good one (it weighs on the mood in its favor). */
bool journal_thought_is_good(ThoughtType type);

/* Adds an entry (the oldest goes when it is full). */
void journal_push(Journal *journal, JournalEntry entry);

/* The `index`-th most recent entry (0: the last one), NULL past the count. */
const JournalEntry *journal_recent(const Journal *journal, int index);

/* Notes down the thoughts that came to the mood since the last call. */
void journal_note_thoughts(Journal *journal, Mood *mood, uint64_t tick);

/* A blow that landed, dealt by `attacker`. */
void journal_note_wound(Journal *journal, uint64_t tick, int attack, const Health *struck, int part,
                        int injury, JournalParty attacker);

/* A wound with no author (a fall, a cave-in, a felled tree): `cause` is a HurtCause. */
void journal_note_hurt(Journal *journal, uint64_t tick, int cause, const Health *struck, int part,
                       int injury);

/* Notes down the wounds `health` took since the last call, by their causes
 * (those of blows are noted by the game with the attacker). */
void journal_note_hurts(Journal *journal, Health *health, uint64_t tick);

/* A blow it gave to `target`: on a hit, `struck`, `part` and `injury` say
 * where and what (`struck` may be NULL on a miss). */
void journal_note_strike(Journal *journal, uint64_t tick, int attack, JournalBlow outcome,
                         const Health *struck, int part, int injury, JournalParty target);

/* A blow it took that did not wound it (missed, dodged, harmless; a wound
 * goes through journal_note_wound()). `part` is where a harmless one hit. */
void journal_note_struck(Journal *journal, uint64_t tick, int attack, JournalBlow outcome,
                         const Health *struck, int part, JournalParty attacker);

/* A chat with a settler: what about (`what` its subject), who brought it up
 * (`heard`: the other), and how it went. */
void journal_note_chat(Journal *journal, uint64_t tick, const char *with, ChatTopic topic, int what,
                       bool heard, ChatQuality quality);

/* The details of a social entry (see the events above). */
typedef struct JournalSocial {
    uint8_t topic;
    uint16_t what;
    uint8_t outcome;
    uint8_t animal; /* Species + 1, 0 for none. */
    uint8_t animal_sex;
    const char *subject; /* NULL for none. */
} JournalSocial;

/* A social event with `with` (JOURNAL_EXPLAINED to JOURNAL_RELATION). */
void journal_note_social(Journal *journal, uint64_t tick, JournalEvent event, const char *with,
                         JournalSocial details);

#endif /* COLONY_CORE_JOURNAL_H */
