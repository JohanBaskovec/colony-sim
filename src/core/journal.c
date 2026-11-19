#include "core/journal.h"

#include <stdio.h>
#include <string.h>

JournalFilter journal_event_kinds(JournalEvent event)
{
    switch (event) {
    case JOURNAL_THOUGHT: return JOURNAL_BIT(JOURNAL_THOUGHTS);
    case JOURNAL_WOUND: return JOURNAL_BIT(JOURNAL_WOUNDS) | JOURNAL_BIT(JOURNAL_ATTACKS);
    case JOURNAL_HURT: return JOURNAL_BIT(JOURNAL_WOUNDS);
    case JOURNAL_CHAT:
    case JOURNAL_EXPLAINED:
    case JOURNAL_LEARNED:
    case JOURNAL_TOLD_STORY:
    case JOURNAL_HEARD_STORY:
    case JOURNAL_LIED:
    case JOURNAL_CAUGHT_LIE:
    case JOURNAL_COMPLIMENT:
    case JOURNAL_COMPLIMENTED:
    case JOURNAL_INSULT:
    case JOURNAL_INSULTED:
    case JOURNAL_GOSSIP:
    case JOURNAL_HEARD_GOSSIP:
    case JOURNAL_REPORTED:
    case JOURNAL_HEARD_REPORT:
    case JOURNAL_ARGUED:
    case JOURNAL_APOLOGIZED:
    case JOURNAL_FORGAVE:
    case JOURNAL_RELATION: return JOURNAL_BIT(JOURNAL_SOCIAL);
    case JOURNAL_BRAWL: return JOURNAL_BIT(JOURNAL_SOCIAL) | JOURNAL_BIT(JOURNAL_ATTACKS);
    case JOURNAL_STRIKE:
    case JOURNAL_STRUCK: return JOURNAL_BIT(JOURNAL_ATTACKS);
    case JOURNAL_EVENT_COUNT: break;
    }
    return 0;
}

JournalFilter journal_entry_kinds(const JournalEntry *entry)
{
    if (entry->event == JOURNAL_THOUGHT && thought_is_routine((ThoughtType)entry->what)) {
        return JOURNAL_BIT(JOURNAL_ROUTINE);
    }
    return journal_event_kinds((JournalEvent)entry->event);
}

bool journal_thought_is_good(ThoughtType type)
{
    const ThoughtDef *def = thought_def(type);
    return def != NULL && def->value > 0.0f;
}

/* Where the entry `age` places from the oldest is kept. */
static int physical_index(const Journal *journal, int age)
{
    return (journal->next - journal->count + age + 2 * JOURNAL_MAX) % JOURNAL_MAX;
}

static bool is_routine(const JournalEntry *entry)
{
    return journal_entry_kinds(entry) == JOURNAL_BIT(JOURNAL_ROUTINE);
}

/* Takes out the oldest routine entry once there are JOURNAL_ROUTINE_MAX of
 * them, the newer ones moving down. */
static void make_room_for_routine(Journal *journal)
{
    int routine = 0, oldest = -1;
    for (int age = 0; age < journal->count; age++) {
        if (is_routine(&journal->list[physical_index(journal, age)])) {
            oldest = routine == 0 ? age : oldest;
            routine++;
        }
    }
    if (routine < JOURNAL_ROUTINE_MAX) {
        return;
    }
    for (int age = oldest; age < journal->count - 1; age++) {
        journal->list[physical_index(journal, age)] =
            journal->list[physical_index(journal, age + 1)];
    }
    journal->next = (journal->next + JOURNAL_MAX - 1) % JOURNAL_MAX;
    journal->count--;
}

void journal_push(Journal *journal, JournalEntry entry)
{
    if (is_routine(&entry)) {
        make_room_for_routine(journal);
    }
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

void journal_note_chat(Journal *journal, uint64_t tick, const char *with, ChatTopic topic, int what,
                       bool heard, ChatQuality quality)
{
    JournalEntry entry = {.tick = tick,
                          .event = JOURNAL_CHAT,
                          .topic = (uint8_t)topic,
                          .what = (uint16_t)what,
                          .heard = heard,
                          .outcome = (uint8_t)quality};
    set_other(&entry, with);
    journal_push(journal, entry);
}

void journal_note_social(Journal *journal, uint64_t tick, JournalEvent event, const char *with,
                         JournalSocial details)
{
    JournalEntry entry = {.tick = tick,
                          .event = (uint8_t)event,
                          .topic = details.topic,
                          .what = details.what,
                          .outcome = details.outcome,
                          .animal = details.animal,
                          .animal_sex = details.animal_sex};
    set_other(&entry, with);
    if (details.subject != NULL) {
        snprintf(entry.subject, sizeof(entry.subject), "%s", details.subject);
    }
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
