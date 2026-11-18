#include "core/journal.h"

#include <string.h>

JournalFilter journal_event_kinds(JournalEvent event)
{
    switch (event) {
    case JOURNAL_THOUGHT: return JOURNAL_BIT(JOURNAL_THOUGHTS);
    case JOURNAL_WOUND: return JOURNAL_BIT(JOURNAL_WOUNDS) | JOURNAL_BIT(JOURNAL_ATTACKS);
    case JOURNAL_HURT: return JOURNAL_BIT(JOURNAL_WOUNDS);
    case JOURNAL_CHAT:
    case JOURNAL_CHAT_ABOUT: return JOURNAL_BIT(JOURNAL_SOCIAL);
    case JOURNAL_STRIKE:
    case JOURNAL_STRUCK: return JOURNAL_BIT(JOURNAL_ATTACKS);
    case JOURNAL_EVENT_COUNT: break;
    }
    return 0;
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

static void set_other(JournalEntry *entry, const char *name);

static void set_party(JournalEntry *entry, JournalParty party)
{
    entry->species = (uint8_t)party.species;
    entry->sex = (uint8_t)(party.species > 0 ? party.sex : 0);
    set_other(entry, party.name);
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
                        int injury, JournalParty attacker)
{
    JournalEntry entry = {.tick = tick,
                          .event = JOURNAL_WOUND,
                          .plan = struck->plan,
                          .part = (uint8_t)part,
                          .injury = (uint8_t)injury,
                          .what = (uint16_t)attack};
    set_party(&entry, attacker);
    journal_push(journal, entry);
}

/* A miss like the last entry, soon after it, is counted in it. */
static void push_blow(Journal *journal, JournalEntry entry)
{
    JournalEntry *last =
        journal->count > 0 ? &journal->list[(journal->next - 1 + JOURNAL_MAX) % JOURNAL_MAX] : NULL;
    bool miss = entry.outcome == JOURNAL_BLOW_MISSED || entry.outcome == JOURNAL_BLOW_DODGED;
    if (miss && last != NULL && last->event == entry.event && last->what == entry.what &&
        last->outcome == entry.outcome && last->species == entry.species &&
        last->sex == entry.sex && strcmp(last->other, entry.other) == 0 &&
        last->repeats < UINT8_MAX && entry.tick - last->tick <= JOURNAL_REPEAT_TICKS) {
        last->repeats++;
        last->tick = entry.tick;
        return;
    }
    journal_push(journal, entry);
}

void journal_note_strike(Journal *journal, uint64_t tick, int attack, JournalBlow outcome,
                         const Health *struck, int part, int injury, JournalParty target)
{
    JournalEntry entry = {.tick = tick,
                          .event = JOURNAL_STRIKE,
                          .plan = struck != NULL ? struck->plan : 0,
                          .part = (uint8_t)(part >= 0 ? part : 0),
                          .injury = (uint8_t)injury,
                          .what = (uint16_t)attack,
                          .outcome = (uint8_t)outcome};
    set_party(&entry, target);
    push_blow(journal, entry);
}

void journal_note_struck(Journal *journal, uint64_t tick, int attack, JournalBlow outcome,
                         const Health *struck, int part, JournalParty attacker)
{
    JournalEntry entry = {.tick = tick,
                          .event = JOURNAL_STRUCK,
                          .plan = struck != NULL ? struck->plan : 0,
                          .part = (uint8_t)(part >= 0 ? part : 0),
                          .what = (uint16_t)attack,
                          .outcome = (uint8_t)outcome};
    set_party(&entry, attacker);
    push_blow(journal, entry);
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
