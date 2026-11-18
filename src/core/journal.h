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
#define JOURNAL_NAME_SIZE 32

typedef enum JournalKind {
    JOURNAL_THOUGHTS,
    JOURNAL_WOUNDS,
    JOURNAL_ATTACKS,
    JOURNAL_SOCIAL,
    JOURNAL_KIND_COUNT
} JournalKind;

/* Which kinds are shown: a bit per JournalKind. */
typedef uint8_t JournalFilter;
#define JOURNAL_BIT(kind) ((JournalFilter)(1u << (unsigned)(kind)))
#define JOURNAL_ALL ((JournalFilter)((1u << JOURNAL_KIND_COUNT) - 1u))

typedef enum JournalEvent {
    JOURNAL_THOUGHT,    /* `what`: a ThoughtType. */
    JOURNAL_WOUND,      /* `what`: an AttackType; plan, part, injury; the attacker. */
    JOURNAL_HURT,       /* `what`: a HurtCause; plan, part, injury (no author). */
    JOURNAL_CHAT,       /* With `other` (a settler). */
    JOURNAL_CHAT_ABOUT, /* With `other`, about the knowledge `what`. */
    /* A blow it gave: `what` an AttackType, `outcome`; on a hit, the plan,
     * part and injury of who it struck, named by `species`/`other`. */
    JOURNAL_STRIKE,
    /* A blow it took that did not wound it (a wound is JOURNAL_WOUND): `what`
     * an AttackType, `outcome`; the attacker; on a hit, the plan and part. */
    JOURNAL_STRUCK,
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
    char other[JOURNAL_NAME_SIZE];
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

/* A chat with a settler, about a knowledge or not (KNOWLEDGE_NONE). */
void journal_note_chat(Journal *journal, uint64_t tick, const char *with, KnowledgeType topic);

#endif /* COLONY_CORE_JOURNAL_H */
