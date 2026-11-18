/* The journal: thoughts, wounds and chats noted down (core/journal.h). */
#include "core/conversation.h"
#include "core/game.h"
#include "core/journal.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <string.h>

/* The newest entries come first, and the oldest are forgotten. */
static void test_ring(void)
{
    static Journal journal;
    memset(&journal, 0, sizeof(journal));
    CHECK(journal_recent(&journal, 0) == NULL);
    for (int i = 0; i < JOURNAL_MAX + 5; i++) {
        journal_note_chat(&journal, (uint64_t)i, "Anna", KNOWLEDGE_NONE);
    }
    CHECK_EQ_INT(journal.count, JOURNAL_MAX);
    CHECK_EQ_UINT(journal_recent(&journal, 0)->tick, JOURNAL_MAX + 4);
    CHECK_EQ_UINT(journal_recent(&journal, JOURNAL_MAX - 1)->tick, 5);
    CHECK(journal_recent(&journal, JOURNAL_MAX) == NULL);
    CHECK(journal_recent(&journal, -1) == NULL);
}

static void test_kinds(void)
{
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_THOUGHT), JOURNAL_BIT(JOURNAL_THOUGHTS));
    /* A wound from a blow is a wound and an attack; one from a fall a wound only. */
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_WOUND),
                 JOURNAL_BIT(JOURNAL_WOUNDS) | JOURNAL_BIT(JOURNAL_ATTACKS));
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_HURT), JOURNAL_BIT(JOURNAL_WOUNDS));
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_CHAT), JOURNAL_BIT(JOURNAL_SOCIAL));
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_CHAT_ABOUT), JOURNAL_BIT(JOURNAL_SOCIAL));
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_STRIKE), JOURNAL_BIT(JOURNAL_ATTACKS));
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_STRUCK), JOURNAL_BIT(JOURNAL_ATTACKS));
    CHECK_EQ_INT(journal_event_kinds(JOURNAL_EVENT_COUNT), 0);
    CHECK(JOURNAL_ALL == (JOURNAL_BIT(JOURNAL_THOUGHTS) | JOURNAL_BIT(JOURNAL_WOUNDS) |
                          JOURNAL_BIT(JOURNAL_SOCIAL) | JOURNAL_BIT(JOURNAL_ATTACKS)));
    CHECK(journal_thought_is_good(THOUGHT_SLEPT_WELL));
    CHECK(!journal_thought_is_good(THOUGHT_SLEPT_ON_GROUND));
    CHECK(!journal_thought_is_good(THOUGHT_STARVING));
    CHECK(!journal_thought_is_good(THOUGHT_COUNT));
}

/* A memory remembered, a state that begins: noted once; a state that goes on
 * is not noted again. */
static void test_thoughts(void)
{
    static Journal journal;
    memset(&journal, 0, sizeof(journal));
    Mood mood;
    mood_init(&mood);
    journal_note_thoughts(&journal, &mood, 10);
    CHECK_EQ_INT(journal.count, 0);
    mood_remember(&mood, THOUGHT_SLEPT_WELL);
    mood_set_states(&mood, THOUGHT_BIT(THOUGHT_HUNGRY));
    journal_note_thoughts(&journal, &mood, 20);
    CHECK_EQ_INT(journal.count, 2);
    CHECK_EQ_INT(journal_recent(&journal, 0)->event, JOURNAL_THOUGHT);
    CHECK_EQ_UINT(journal_recent(&journal, 0)->tick, 20);
    bool well = false, hungry = false;
    for (int i = 0; i < 2; i++) {
        well |= journal_recent(&journal, i)->what == THOUGHT_SLEPT_WELL;
        hungry |= journal_recent(&journal, i)->what == THOUGHT_HUNGRY;
    }
    CHECK(well && hungry);
    /* Still hungry: nothing new. Hungrier: a new thought. */
    mood_set_states(&mood, THOUGHT_BIT(THOUGHT_HUNGRY));
    journal_note_thoughts(&journal, &mood, 30);
    CHECK_EQ_INT(journal.count, 2);
    mood_set_states(&mood, THOUGHT_BIT(THOUGHT_STARVING));
    journal_note_thoughts(&journal, &mood, 40);
    CHECK_EQ_INT(journal.count, 3);
    CHECK_EQ_INT(journal_recent(&journal, 0)->what, THOUGHT_STARVING);
    /* The journal is not part of what the mood hashes. */
    uint64_t hash = mood_hash(&mood, 1);
    mood_remember(&mood, THOUGHT_CHAT);
    mood.news = 0;
    Mood other = mood;
    other.news = THOUGHT_BIT(THOUGHT_CHAT);
    CHECK(mood_hash(&other, 1) == mood_hash(&mood, 1));
    CHECK(hash != mood_hash(&mood, 1));
}

static void test_wound_entry(void)
{
    static Journal journal;
    memset(&journal, 0, sizeof(journal));
    Health health;
    health_init(&health, BODY_HUMAN, 1.0f, 1.0f, 1.0f);
    journal_note_wound(&journal, 7, ATTACK_WOLF_BITE, &health, 4, INJURY_BITE,
                       (JournalParty){NULL, 5, ANIMAL_MALE});
    journal_note_wound(&journal, 8, ATTACK_FISTS, &health, 2, INJURY_BRUISE,
                       (JournalParty){"A very long name that does not fit in the entry", 0, 0});
    const JournalEntry *fists = journal_recent(&journal, 0);
    const JournalEntry *bite = journal_recent(&journal, 1);
    CHECK_EQ_INT(bite->event, JOURNAL_WOUND);
    CHECK_EQ_INT(bite->what, ATTACK_WOLF_BITE);
    CHECK_EQ_INT(bite->plan, BODY_HUMAN);
    CHECK_EQ_INT(bite->part, 4);
    CHECK_EQ_INT(bite->injury, INJURY_BITE);
    CHECK_EQ_INT(bite->species, 5);
    CHECK_EQ_INT(bite->sex, ANIMAL_MALE);
    CHECK_EQ_INT(fists->species, 0);
    CHECK_EQ_INT((int)strlen(fists->other), JOURNAL_NAME_SIZE - 1); /* Cut, still a string. */
}

/* Blows given and taken; the same miss again soon is counted, not repeated. */
static void test_blow_entries(void)
{
    static Journal journal;
    memset(&journal, 0, sizeof(journal));
    Health wolf;
    health_init(&wolf, BODY_CANINE, 0.7f, 1.0f, 1.0f);
    journal_note_strike(&journal, 10, ATTACK_SPEAR_THRUST, JOURNAL_BLOW_WOUNDED, &wolf, 1,
                        INJURY_CUT, (JournalParty){NULL, ANIMAL_WOLF + 1, ANIMAL_FEMALE});
    const JournalEntry *strike = journal_recent(&journal, 0);
    CHECK_EQ_INT(strike->event, JOURNAL_STRIKE);
    CHECK_EQ_INT(strike->outcome, JOURNAL_BLOW_WOUNDED);
    CHECK_EQ_INT(strike->what, ATTACK_SPEAR_THRUST);
    CHECK_EQ_INT(strike->plan, BODY_CANINE);
    CHECK_EQ_INT(strike->part, 1);
    CHECK_EQ_INT(strike->injury, INJURY_CUT);
    CHECK_EQ_INT(strike->species, ANIMAL_WOLF + 1);
    CHECK_EQ_INT(strike->sex, ANIMAL_FEMALE);
    CHECK_EQ_INT(strike->repeats, 0);
    /* A miss on no part. */
    journal_note_struck(&journal, 20, ATTACK_WOLF_BITE, JOURNAL_BLOW_DODGED, &wolf, -1,
                        (JournalParty){NULL, ANIMAL_WOLF + 1, ANIMAL_FEMALE});
    CHECK_EQ_INT(journal.count, 2);
    CHECK_EQ_INT(journal_recent(&journal, 0)->event, JOURNAL_STRUCK);
    CHECK_EQ_INT(journal_recent(&journal, 0)->part, 0);
    /* Dodged again soon: counted in the same entry, which moves to the time. */
    journal_note_struck(&journal, 80, ATTACK_WOLF_BITE, JOURNAL_BLOW_DODGED, &wolf, -1,
                        (JournalParty){NULL, ANIMAL_WOLF + 1, ANIMAL_FEMALE});
    journal_note_struck(&journal, 140, ATTACK_WOLF_BITE, JOURNAL_BLOW_DODGED, &wolf, -1,
                        (JournalParty){NULL, ANIMAL_WOLF + 1, ANIMAL_FEMALE});
    CHECK_EQ_INT(journal.count, 2);
    CHECK_EQ_INT(journal_recent(&journal, 0)->repeats, 2);
    CHECK_EQ_UINT(journal_recent(&journal, 0)->tick, 140);
    /* Another outcome, another attack, another attacker, or long after: new entries. */
    journal_note_struck(&journal, 150, ATTACK_WOLF_BITE, JOURNAL_BLOW_MISSED, &wolf, -1,
                        (JournalParty){NULL, ANIMAL_WOLF + 1, ANIMAL_FEMALE});
    journal_note_struck(&journal, 160, ATTACK_WOLF_CLAWS, JOURNAL_BLOW_MISSED, &wolf, -1,
                        (JournalParty){NULL, ANIMAL_WOLF + 1, ANIMAL_FEMALE});
    journal_note_struck(&journal, 165, ATTACK_WOLF_CLAWS, JOURNAL_BLOW_MISSED, &wolf, -1,
                        (JournalParty){NULL, ANIMAL_WOLF + 1, ANIMAL_MALE}); /* Another wolf. */
    journal_note_struck(&journal, 170, ATTACK_WOLF_CLAWS, JOURNAL_BLOW_MISSED, &wolf, -1,
                        (JournalParty){"Anna", 0, 0});
    journal_note_struck(&journal, 170 + JOURNAL_REPEAT_TICKS + 1, ATTACK_WOLF_CLAWS,
                        JOURNAL_BLOW_MISSED, &wolf, -1, (JournalParty){"Anna", 0, 0});
    CHECK_EQ_INT(journal.count, 7);
    /* Hits are never merged: each says where it landed. */
    journal_note_struck(&journal, 1000, ATTACK_FISTS, JOURNAL_BLOW_HARMLESS, &wolf, 2,
                        (JournalParty){"Anna", 0, 0});
    journal_note_struck(&journal, 1001, ATTACK_FISTS, JOURNAL_BLOW_HARMLESS, &wolf, 2,
                        (JournalParty){"Anna", 0, 0});
    CHECK_EQ_INT(journal.count, 9);
}

/* A chat goes in both journals, with the other's name and what it was about. */
static void test_chat_in_a_game(void)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 32;
    params.world.height = 32;
    params.colonist_count = 0;
    REQUIRE(game_init(&game, &params));
    for (size_t i = 0; i < map_level_tile_count(&game.map); i++) {
        game.map.terrain[i] = TERRAIN_SOIL;
        game.map.rock[i] = ROCK_NONE;
        memset(&game.map.plants[i], 0, sizeof(game.map.plants[i]));
    }
    map_topology_changed(&game.map);
    EntityId a_id = game_spawn_colonist(&game, 4, 4, 0);
    EntityId b_id = game_spawn_colonist(&game, 5, 4, 0);
    Colonist *a = game_colonist(&game, a_id), *b = game_colonist(&game, b_id);
    REQUIRE(a != NULL && b != NULL);
    int chats = 0;
    for (int i = 0; i < 50 && chats < 3; i++) {
        Rng rng;
        rng_seed(&rng, 100 + (uint64_t)i, 1);
        conversation_hold(a, a_id, b, b_id, 500 + (uint64_t)i, &rng);
        chats++;
    }
    CHECK_EQ_INT(a->journal.count, chats);
    CHECK_EQ_INT(b->journal.count, chats);
    const JournalEntry *entry = journal_recent(&a->journal, 0);
    CHECK(entry->event == JOURNAL_CHAT || entry->event == JOURNAL_CHAT_ABOUT);
    CHECK(strcmp(entry->other, b->name) == 0);
    CHECK(strcmp(journal_recent(&b->journal, 0)->other, a->name) == 0);
    CHECK_EQ_INT(journal_recent(&b->journal, 0)->what, entry->what);
    /* Their chat thought was noted by the game, on the next tick. */
    game_tick(&game);
    bool thought = false;
    for (int i = 0; i < a->journal.count; i++) {
        const JournalEntry *each = journal_recent(&a->journal, i);
        thought |= each->event == JOURNAL_THOUGHT && each->what == THOUGHT_CHAT;
    }
    CHECK(thought);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_ring);
    RUN_TEST(test_kinds);
    RUN_TEST(test_thoughts);
    RUN_TEST(test_wound_entry);
    RUN_TEST(test_blow_entries);
    RUN_TEST(test_chat_in_a_game);
    return test_summary();
}
