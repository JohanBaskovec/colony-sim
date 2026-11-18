#include "core/journal.h"

#include <string.h>

JournalKind journal_event_kind(JournalEvent event)
{
    switch (event) {
    case JOURNAL_THOUGHT: return JOURNAL_THOUGHTS;
    case JOURNAL_WOUND:
    case JOURNAL_HURT: return JOURNAL_WOUNDS;
    case JOURNAL_CHAT:
    case JOURNAL_CHAT_ABOUT: return JOURNAL_SOCIAL;
    case JOURNAL_EVENT_COUNT: break;
    }
    return JOURNAL_KIND_COUNT;
}

bool journal_thought_is_good(ThoughtType type)
{
    const ThoughtDef *def = thought_def(type);
    return def != NULL && def->value > 0.0f;
}

void journal_push(Journal *journal, JournalEntry entry)
{
    journal->list[journal->next] = entry;
    journal->next = (journal->next + 1) % JOURNAL_MAX;
    if (journal->count < JOURNAL_MAX) {
        journal->count++;
    }
}

const JournalEntry *journal_recent(const Journal *journal, int index)
{
    if (index < 0 || index >= journal->count) {
        return NULL;
    }
    return &journal->list[(journal->next - 1 - index + JOURNAL_MAX) % JOURNAL_MAX];
}

static void set_other(JournalEntry *entry, const char *name)
{
    if (name != NULL) {
        strncpy(entry->other, name, JOURNAL_NAME_SIZE - 1);
        entry->other[JOURNAL_NAME_SIZE - 1] = '\0';
    }
}

void journal_note_thoughts(Journal *journal, Mood *mood, uint64_t tick)
{
    ThoughtMask news = mood_take_news(mood);
    for (int type = 0; news != 0 && type < THOUGHT_COUNT; type++) {
        if ((news & THOUGHT_BIT(type)) != 0) {
            journal_push(
                journal,
                (JournalEntry){.tick = tick, .event = JOURNAL_THOUGHT, .what = (uint16_t)type});
        }
    }
}

void journal_note_wound(Journal *journal, uint64_t tick, int attack, const Health *struck, int part,
                        int injury, const char *attacker, int species)
{
    JournalEntry entry = {.tick = tick,
                          .event = JOURNAL_WOUND,
                          .plan = struck->plan,
                          .part = (uint8_t)part,
                          .injury = (uint8_t)injury,
                          .what = (uint16_t)attack,
                          .species = (uint8_t)species};
    set_other(&entry, attacker);
    journal_push(journal, entry);
}

void journal_note_chat(Journal *journal, uint64_t tick, const char *with, KnowledgeType topic)
{
    JournalEntry entry = {.tick = tick,
                          .event = topic == KNOWLEDGE_NONE ? JOURNAL_CHAT : JOURNAL_CHAT_ABOUT,
                          .what = (uint16_t)topic};
    set_other(&entry, with);
    journal_push(journal, entry);
}

void journal_note_hurt(Journal *journal, uint64_t tick, int cause, const Health *struck, int part,
                       int injury)
{
    journal_push(journal, (JournalEntry){.tick = tick,
                                         .event = JOURNAL_HURT,
                                         .plan = struck->plan,
                                         .part = (uint8_t)part,
                                         .injury = (uint8_t)injury,
                                         .what = (uint16_t)cause});
}

void journal_note_hurts(Journal *journal, Health *health, uint64_t tick)
{
    HurtNote notes[HEALTH_NOTES_MAX];
    int count = health_take_notes(health, notes);
    for (int i = 0; i < count; i++) {
        journal_note_hurt(journal, tick, notes[i].cause, health, notes[i].part, notes[i].type);
    }
}
