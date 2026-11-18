/*
 * The journal of a settler or an animal: what happened to it, newest last in
 * a small ring (the oldest are forgotten). Three kinds of entries, which the
 * player may filter: new thoughts (good and bad), wounds (who struck it, with
 * what, where, and what it caused) and social life (chats).
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

#define JOURNAL_MAX 40
#define JOURNAL_NAME_SIZE 32

typedef enum JournalKind {
    JOURNAL_THOUGHTS,
    JOURNAL_WOUNDS,
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
    JOURNAL_EVENT_COUNT
} JournalEvent;

typedef struct JournalEntry {
    uint64_t tick;
    uint8_t event;  /* JournalEvent */
    uint8_t plan;   /* Wounds: BodyPlan of who was struck... */
    uint8_t part;   /* ...the part, in that plan... */
    uint8_t injury; /* ...and what the blow caused (InjuryType). */
    uint16_t what;
    /* Who the entry is about: an animal's species + 1 (the wounds it dealt), or
     * 0 for a settler, then named in `other`. */
    uint8_t species;
    char other[JOURNAL_NAME_SIZE];
} JournalEntry;

typedef struct Journal {
    JournalEntry list[JOURNAL_MAX];
    int next;  /* Where the next entry goes. */
    int count; /* Up to JOURNAL_MAX. */
} Journal;

/* The kind an event is filed under (JOURNAL_KIND_COUNT for an invalid one). */
JournalKind journal_event_kind(JournalEvent event);

/* Whether a thought is a good one (it weighs on the mood in its favor). */
bool journal_thought_is_good(ThoughtType type);

/* Adds an entry (the oldest goes when it is full). */
void journal_push(Journal *journal, JournalEntry entry);

/* The `index`-th most recent entry (0: the last one), NULL past the count. */
const JournalEntry *journal_recent(const Journal *journal, int index);

/* Notes down the thoughts that came to the mood since the last call. */
void journal_note_thoughts(Journal *journal, Mood *mood, uint64_t tick);

/* A blow that landed: `attacker` names a settler (`species` 0), else an
 * animal's species + 1. */
void journal_note_wound(Journal *journal, uint64_t tick, int attack, const Health *struck, int part,
                        int injury, const char *attacker, int species);

/* A wound with no author (a fall, a cave-in, a felled tree): `cause` is a HurtCause. */
void journal_note_hurt(Journal *journal, uint64_t tick, int cause, const Health *struck, int part,
                       int injury);

/* Notes down the wounds `health` took since the last call, by their causes
 * (those of blows are noted by the game with the attacker). */
void journal_note_hurts(Journal *journal, Health *health, uint64_t tick);

/* A chat with a settler, about a knowledge or not (KNOWLEDGE_NONE). */
void journal_note_chat(Journal *journal, uint64_t tick, const char *with, KnowledgeType topic);

#endif /* COLONY_CORE_JOURNAL_H */
