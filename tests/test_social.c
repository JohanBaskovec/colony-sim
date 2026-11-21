/* Social life: opinions and their memories, families, relationships,
 * experiences, chats, company (core/social.h, core/experience.h,
 * core/conversation.h). */
#include "core/conversation.h"
#include "core/experience.h"
#include "core/game.h"
#include "core/social.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <math.h>
#include <string.h>

#define HOUR ((uint64_t)TICKS_PER_HOUR)
#define YEAR ((uint64_t)TICKS_PER_YEAR)

/* A few settlers outside a game: ordinary, side by side, all alive. */
typedef struct Town {
    Colonist colonists[COLONIST_MAX];
    Pool pool;
    Social social;
    int count;
} Town;

static Town town;

static void town_init(int count, const int *ages)
{
    static const char *const NAMES[] = {"Anna", "Bo", "Cyd", "Dana", "Eli", "Fen", "Gil", "Hal"};
    memset(&town.colonists, 0, sizeof(town.colonists));
    REQUIRE(pool_init(&town.pool, COLONIST_MAX));
    social_init(&town.social, 99);
    town.count = count;
    for (int i = 0; i < count; i++) {
        pool_alloc(&town.pool);
        colonist_init(&town.colonists[i], NAMES[i % 8], ages != NULL ? ages[i] : 30, i, 0, 0);
        make_ordinary(&town.colonists[i]);
    }
}

static void town_free(void)
{
    for (int i = 0; i < town.count; i++) {
        colonist_free(&town.colonists[i]);
    }
    pool_free(&town.pool);
}

static ConversationWorld town_world(uint64_t tick)
{
    return (ConversationWorld){town.colonists, &town.pool, &town.social, tick, WEATHER_RAIN, NULL};
}

static float town_opinion(int who, int whom, uint64_t tick)
{
    return social_opinion(&town.social, town.colonists, who, whom, tick);
}

/* ---- Memories ---------------------------------------------------------------- */

/* A memory weighs its full value through its plateau, then halves every
 * half-life; one of a killing in the family is still there after 50 years. */
static void test_memories_fade_over_years(void)
{
    town_init(2, NULL);
    social_remember(&town.social, 0, 1, SOCIAL_MEMORY_AWKWARD_CHAT, 1.0f, 0);
    const SocialMemoryDef *awkward = social_memory_def(SOCIAL_MEMORY_AWKWARD_CHAT);
    CHECK_NEAR(town_opinion(0, 1, 0), awkward->value, 1e-4);
    uint64_t plateau = (uint64_t)awkward->plateau_hours * HOUR;
    uint64_t half = (uint64_t)awkward->half_life_hours * HOUR;
    CHECK_NEAR(town_opinion(0, 1, plateau), awkward->value, 1e-4);
    CHECK_NEAR(town_opinion(0, 1, plateau + half), awkward->value / 2.0f, 1e-3);
    /* Forgotten within weeks, and dropped once forgotten. */
    social_update(&town.social, town.colonists, &town.pool, 60 * 24 * HOUR, false);
    CHECK_EQ_INT(social_bond(&town.social, 0, 1)->memory_count, 0);

    social_remember(&town.social, 0, 1, SOCIAL_MEMORY_KILLED_FAMILY, 1.0f, 0);
    CHECK(town_opinion(0, 1, 10 * YEAR) <= -79.0f);
    float fifty = town_opinion(0, 1, 50 * YEAR);
    CHECK(fifty < -15.0f && fifty > -25.0f); /* A quarter of it. */
    CHECK(town_opinion(0, 1, 100 * YEAR) > -3.0f);
    /* Opinions are one way: the killer was not wronged. */
    CHECK_NEAR(town_opinion(1, 0, 10 * YEAR), 0.0f, 1e-4);
    town_free();
}

/* The same memory again adds up to its limit and starts its plateau over;
 * a full bond lets the weakest memory go. */
static void test_memories_stack_and_make_room(void)
{
    town_init(2, NULL);
    const SocialMemoryDef *chat = social_memory_def(SOCIAL_MEMORY_PLEASANT_CHAT);
    for (int i = 0; i < 40; i++) {
        social_remember(&town.social, 0, 1, SOCIAL_MEMORY_PLEASANT_CHAT, 1.0f, (uint64_t)i * HOUR);
    }
    const SocialBond *bond = social_bond_const(&town.social, 0, 1);
    CHECK_EQ_INT(bond->memory_count, 1);
    CHECK_EQ_INT(bond->memories[0].count, 40);
    CHECK_NEAR(town_opinion(0, 1, 40 * HOUR), chat->value * (float)chat->max_stack, 1e-3);
    /* Fill the bond with stronger memories: the chats go last. */
    static const SocialMemoryType OTHERS[] = {
        SOCIAL_MEMORY_JOURNEY,     SOCIAL_MEMORY_TAUGHT_ME,     SOCIAL_MEMORY_GOOD_STORY,
        SOCIAL_MEMORY_CONFIDED,    SOCIAL_MEMORY_INSULTED_ME,   SOCIAL_MEMORY_LIED_TO_ME,
        SOCIAL_MEMORY_ATTACKED_ME, SOCIAL_MEMORY_KILLED_FRIEND,
    };
    for (size_t i = 0; i < sizeof(OTHERS) / sizeof(OTHERS[0]); i++) {
        social_remember(&town.social, 0, 1, OTHERS[i], 5.0f, 200 * HOUR);
    }
    CHECK_EQ_INT(bond->memory_count, SOCIAL_MEMORY_MAX);
    bool chats = false;
    for (int i = 0; i < bond->memory_count; i++) {
        chats |= bond->memories[i].type == SOCIAL_MEMORY_PLEASANT_CHAT;
    }
    CHECK(!chats);
    /* A weak one does not push out a strong one. */
    social_remember(&town.social, 0, 1, SOCIAL_MEMORY_GOOD_WORD, 0.1f, 200 * HOUR);
    for (int i = 0; i < bond->memory_count; i++) {
        CHECK(bond->memories[i].type != SOCIAL_MEMORY_GOOD_WORD);
    }
    town_free();
}

/* Family, looks and confidence count; opinions stay within ±100. */
static void test_opinion_shares(void)
{
    town_init(3, NULL);
    CHECK(social_set_family(&town.social, 0, 1, FAMILY_SIBLING));
    CHECK_EQ_INT(social_bond(&town.social, 1, 0)->family, FAMILY_SIBLING);
    CHECK(social_set_family(&town.social, 0, 2, FAMILY_PARENT));
    CHECK_EQ_INT(social_bond(&town.social, 2, 0)->family, FAMILY_CHILD);
    CHECK(!social_set_family(&town.social, 0, 0, FAMILY_SIBLING));
    CHECK_NEAR(town_opinion(0, 1, 0), family_tie_points(FAMILY_SIBLING), 1e-4);
    CHECK_NEAR(town_opinion(2, 0, 0), family_tie_points(FAMILY_CHILD), 1e-4);
    /* Striking and self-assured: liked at first sight. */
    Attributes attributes = town.colonists[1].attributes;
    attributes_set(&attributes, ATTRIBUTE_BEAUTY, ATTRIBUTE_POSITION_MAX);
    attributes_set(&attributes, ATTRIBUTE_CONFIDENCE, ATTRIBUTE_POSITION_MAX);
    colonist_set_attributes(&town.colonists[1], &attributes);
    CHECK_NEAR(town_opinion(0, 1, 0),
               family_tie_points(FAMILY_SIBLING) + SOCIAL_BEAUTY_POINTS + SOCIAL_CONFIDENCE_POINTS,
               1e-3);
    CHECK_NEAR(town_opinion(2, 1, 0), SOCIAL_BEAUTY_POINTS + SOCIAL_CONFIDENCE_POINTS, 1e-3);
    for (int i = 0; i < 5; i++) {
        social_remember(&town.social, 2, 1, SOCIAL_MEMORY_KILLED_FAMILY, 1.0f, 0);
    }
    CHECK_NEAR(town_opinion(2, 1, 0), -SOCIAL_OPINION_MAX, 1e-4);
    /* The reasons, largest first. */
    SocialReason reasons[SOCIAL_REASON_MAX];
    int count = social_reasons(&town.social, town.colonists, 0, 1, 0, reasons, SOCIAL_REASON_MAX);
    REQUIRE(count == 3);
    CHECK_STR_EQ(reasons[0].key, family_tie_key(FAMILY_SIBLING));
    CHECK_STR_EQ(reasons[1].key, "social.reason.looks");
    CHECK_NEAR(reasons[2].value, SOCIAL_CONFIDENCE_POINTS, 1e-4);
    CHECK_EQ_INT(social_reasons(&town.social, town.colonists, 0, 1, 0, reasons, 1), 1);
    town_free();
}

/* Relationships follow opinions, with slack, and the journal tells. */
static void test_relationships_change_with_slack(void)
{
    CHECK_EQ_INT(social_level_of(0.0f), RELATION_ACQUAINTANCE);
    CHECK_EQ_INT(social_level_of(SOCIAL_FRIEND_FROM), RELATION_FRIEND);
    CHECK_EQ_INT(social_level_of(SOCIAL_CLOSE_FRIEND_FROM), RELATION_CLOSE_FRIEND);
    CHECK_EQ_INT(social_level_of(SOCIAL_RIVAL_TO), RELATION_RIVAL);
    CHECK_EQ_INT(social_level_of(SOCIAL_ENEMY_TO), RELATION_ENEMY);
    /* A friend just below the threshold stays a friend... */
    CHECK_EQ_INT(social_level_after(SOCIAL_FRIEND_FROM - 1.0f, RELATION_FRIEND), RELATION_FRIEND);
    /* ...and an acquaintance just past it stays one. */
    CHECK_EQ_INT(social_level_after(SOCIAL_FRIEND_FROM + 1.0f, RELATION_ACQUAINTANCE),
                 RELATION_ACQUAINTANCE);
    CHECK_EQ_INT(social_level_after(SOCIAL_FRIEND_FROM + SOCIAL_LEVEL_SLACK, RELATION_ACQUAINTANCE),
                 RELATION_FRIEND);
    CHECK_EQ_INT(social_level_after(-80.0f, RELATION_FRIEND), RELATION_ENEMY);

    town_init(2, NULL);
    social_remember(&town.social, 0, 1, SOCIAL_MEMORY_JOURNEY, 2.0f, 0);
    CHECK_EQ_INT(social_update(&town.social, town.colonists, &town.pool, 0, true), 1);
    CHECK_EQ_INT(social_bond(&town.social, 0, 1)->level, RELATION_FRIEND);
    const JournalEntry *entry = journal_recent(&town.colonists[0].journal, 0);
    REQUIRE(entry != NULL);
    CHECK_EQ_INT(entry->event, JOURNAL_RELATION);
    CHECK_EQ_INT(entry->what, RELATION_FRIEND);
    CHECK_EQ_INT(entry->outcome, RELATION_ACQUAINTANCE);
    CHECK_STR_EQ(entry->other, "Bo");
    CHECK_EQ_INT(town.colonists[1].journal.count, 0); /* Bo thinks nothing of Anna yet. */
    /* Nothing new: no note. */
    CHECK_EQ_INT(social_update(&town.social, town.colonists, &town.pool, HOUR, true), 0);
    town_free();
}

/* A freed or new slot starts with nobody thinking anything. */
static void test_slots_are_cleared(void)
{
    town_init(3, NULL);
    social_set_family(&town.social, 0, 1, FAMILY_SIBLING);
    social_remember(&town.social, 2, 1, SOCIAL_MEMORY_INSULTED_ME, 1.0f, 0);
    social_remember(&town.social, 1, 2, SOCIAL_MEMORY_TAUGHT_ME, 1.0f, 0);
    social_clear_slot(&town.social, 1);
    CHECK_EQ_INT(social_bond(&town.social, 0, 1)->family, FAMILY_NONE);
    CHECK_EQ_INT(social_bond(&town.social, 2, 1)->memory_count, 0);
    CHECK_EQ_INT(social_bond(&town.social, 1, 2)->memory_count, 0);
    CHECK_EQ_INT(social_bond(&town.social, 1, 2)->level, RELATION_ACQUAINTANCE);
    town_free();
}

/* ---- Families ---------------------------------------------------------------- */

/* Families drawn from ages make sense: ties go both ways, parents are old
 * enough, nobody has more than two, siblings are close in age. */
static void test_families_follow_ages(void)
{
    static const int AGES[] = {62, 58, 40, 37, 35, 19, 18, 21};
    int ties = 0;
    for (uint64_t seed = 1; seed <= 40; seed++) {
        town_init(8, AGES);
        social_init(&town.social, seed);
        ties += social_make_families(&town.social, town.colonists, &town.pool);
        for (int a = 0; a < 8; a++) {
            int parents = 0;
            for (int b = 0; b < 8; b++) {
                if (a == b) {
                    continue;
                }
                FamilyTie tie = (FamilyTie)social_bond(&town.social, a, b)->family;
                CHECK_EQ_INT(social_bond(&town.social, b, a)->family, family_tie_reverse(tie));
                int gap = AGES[b] - AGES[a];
                if (tie == FAMILY_PARENT) {
                    parents++;
                    CHECK(gap >= SOCIAL_PARENT_GAP_MIN && gap <= SOCIAL_PARENT_GAP_MAX);
                } else if (tie == FAMILY_GRANDPARENT) {
                    CHECK(gap >= SOCIAL_GRANDPARENT_GAP);
                } else if (tie == FAMILY_SIBLING) {
                    CHECK(gap <= SOCIAL_PARENT_GAP_MAX - SOCIAL_PARENT_GAP_MIN &&
                          gap >= -(SOCIAL_PARENT_GAP_MAX - SOCIAL_PARENT_GAP_MIN));
                }
            }
            CHECK(parents <= 2);
        }
        town_free();
    }
    CHECK(ties > 40); /* Families are common, not the rule. */
    CHECK(ties < 40 * 28);
}

/* The starting settlers know each other: families, and the journey, which
 * makes every opinion of each other a good one. */
static void test_starting_settlers_like_each_other(void)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 42;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = 8;
    REQUIRE(game_init(&game, &params));
    int pairs = 0, family = 0;
    for (uint32_t a = 0; a < game.colonist_pool.high_water; a++) {
        for (uint32_t b = 0; b < game.colonist_pool.high_water; b++) {
            if (a == b) {
                continue;
            }
            pairs++;
            float opinion =
                social_opinion(&game.social, game.colonists, (int)a, (int)b, game.clock.tick);
            CHECK(opinion > 0.0f);
            const SocialBond *bond = social_bond_const(&game.social, (int)a, (int)b);
            family += bond->family != FAMILY_NONE;
            CHECK_EQ_INT(bond->level, social_level_of(opinion));
            CHECK_EQ_INT(game.colonists[a].journal.count, 0); /* No note for the start. */
        }
    }
    CHECK_EQ_INT(pairs, 8 * 7);
    (void)family;
    game_shutdown(&game);
}

/* ---- Grief and violence ------------------------------------------------------ */

static void test_grief_and_killings(void)
{
    town_init(4, NULL);
    social_set_family(&town.social, 0, 1, FAMILY_SIBLING);
    social_remember(&town.social, 2, 1, SOCIAL_MEMORY_JOURNEY, 2.0f, 0); /* A friend. */
    CHECK_EQ_INT(social_grief(&town.social, town.colonists, 0, 1, 0), THOUGHT_FAMILY_DIED);
    CHECK_EQ_INT(social_grief(&town.social, town.colonists, 2, 1, 0), THOUGHT_FRIEND_DIED);
    CHECK_EQ_INT(social_grief(&town.social, town.colonists, 3, 1, 0), THOUGHT_DEATH);
    /* Dana kills Bo: Bo remembers the attack, Anna the death of a sibling,
     * Cyd that of a friend. */
    social_note_attack(&town.social, town.colonists, &town.pool, 3, 1, true, 0);
    CHECK(town_opinion(1, 3, 0) < 0.0f);
    CHECK(town_opinion(0, 3, 0) <= -79.0f);
    CHECK(town_opinion(2, 3, 0) <= -44.0f);
    CHECK(town_opinion(0, 3, 50 * YEAR) < -15.0f); /* "Killed my brother", 50 years on. */
    CHECK(town_opinion(2, 3, 50 * YEAR) > town_opinion(0, 3, 50 * YEAR));
    town_free();
}

/* ---- Experiences --------------------------------------------------------------- */

static void test_experiences_ring(void)
{
    static Experiences experiences;
    experiences_init(&experiences);
    Rng rng;
    rng_seed(&rng, 3, 1);
    CHECK(experiences_pick(&experiences, 0, &rng) == NULL);
    experiences_add(&experiences, EXPERIENCE_KILLED_ANIMAL, ANIMAL_HARE, ANIMAL_MALE, NULL, 10);
    experiences_add(&experiences, EXPERIENCE_KILLED_ANIMAL, ANIMAL_HARE, ANIMAL_FEMALE, NULL, 12);
    CHECK_EQ_INT(experiences.count, 1); /* One hunt of hares. */
    CHECK_EQ_INT(experiences_recent(&experiences, 0)->count, 2);
    experiences_add(&experiences, EXPERIENCE_SAW_DEATH, 0, 0, "Anna", 30);
    CHECK_STR_EQ(experiences_recent(&experiences, 0)->name, "Anna");
    experiences_add(&experiences, EXPERIENCE_NONE, 0, 0, NULL, 30);
    CHECK_EQ_INT(experiences.count, 2);
    for (uint32_t i = 0; i < EXPERIENCE_MAX + 3; i++) {
        experiences_add(&experiences, EXPERIENCE_COLD_NIGHT, 0, 0, NULL, 100 + 24 * i);
    }
    CHECK_EQ_INT(experiences.count, EXPERIENCE_MAX);
    CHECK_EQ_INT(experiences_recent(&experiences, EXPERIENCE_MAX - 1)->type, EXPERIENCE_COLD_NIGHT);
    CHECK(experiences_recent(&experiences, EXPERIENCE_MAX) == NULL);
    /* Fresh stories are told more than old ones. */
    experiences_init(&experiences);
    experiences_add(&experiences, EXPERIENCE_STARVED, 0, 0, NULL, 0);
    experiences_add(&experiences, EXPERIENCE_PARCHED, 0, 0, NULL, 24 * 60);
    int fresh = 0;
    for (int i = 0; i < 1000; i++) {
        fresh += experiences_pick(&experiences, 24 * 60, &rng)->type == EXPERIENCE_PARCHED;
    }
    CHECK(fresh > 700);
    CHECK(experience_is_feat(EXPERIENCE_KILLED_ANIMAL));
    CHECK(!experience_is_feat(EXPERIENCE_COLD_NIGHT));
}

/* ---- Chats --------------------------------------------------------------------- */

/* Kind settlers pay compliments, abrasive ones insult: opinions and moods follow. */
static void test_compliments_and_insults(void)
{
    town_init(2, NULL);
    ConversationWorld world = town_world(0);
    ChatResult result = conversation_hold_as(&world, 0, 1, CHAT_COMPLIMENT);
    CHECK_EQ_INT(result.kind, CHAT_COMPLIMENT);
    CHECK(town_opinion(1, 0, 0) > 0.0f);
    CHECK(mood_has(&town.colonists[1].mood, THOUGHT_COMPLIMENTED));
    CHECK_EQ_INT(journal_recent(&town.colonists[0].journal, 0)->event, JOURNAL_COMPLIMENT);
    CHECK_EQ_INT(journal_recent(&town.colonists[1].journal, 0)->event, JOURNAL_COMPLIMENTED);
    result = conversation_hold_as(&world, 0, 1, CHAT_INSULT);
    CHECK_EQ_INT(result.kind, CHAT_INSULT);
    CHECK(mood_has(&town.colonists[1].mood, THOUGHT_INSULTED));
    CHECK_EQ_INT(journal_recent(&town.colonists[1].journal, 0)->event, JOURNAL_INSULTED);
    float after_insult = town_opinion(1, 0, 0);
    for (int i = 0; i < 5; i++) {
        conversation_hold_as(&world, 0, 1, CHAT_INSULT);
    }
    CHECK(town_opinion(1, 0, 0) < after_insult);
    CHECK(town_opinion(1, 0, 0) < SOCIAL_RIVAL_TO);
    /* A soaked settler hears about its smell. */
    town.colonists[1].wetness = 1.0f;
    bool smell = false;
    for (int i = 0; i < 40 && !smell; i++) {
        conversation_hold_as(&world, 0, 1, CHAT_INSULT);
        smell = journal_recent(&town.colonists[1].journal, 0)->what == INSULT_SMELL;
    }
    CHECK(smell);

    /* Left to themselves, kind settlers compliment, abrasive ones insult. */
    Attributes kind = town.colonists[0].attributes;
    attributes_set(&kind, ATTRIBUTE_KINDNESS, ATTRIBUTE_POSITION_MAX);
    colonist_set_attributes(&town.colonists[0], &kind);
    Attributes abrasive = town.colonists[1].attributes;
    attributes_set(&abrasive, ATTRIBUTE_KINDNESS, -ATTRIBUTE_POSITION_MAX);
    colonist_set_attributes(&town.colonists[1], &abrasive);
    social_clear_slot(&town.social, 0);
    int compliments = 0, insults = 0, reverse_compliments = 0;
    for (int i = 0; i < 600; i++) {
        world.tick = (uint64_t)i * HOUR;
        compliments += conversation_hold(&world, 0, 1).kind == CHAT_COMPLIMENT;
    }
    for (int i = 0; i < 600; i++) {
        world.tick = (uint64_t)(600 + i) * HOUR;
        ChatKind kind_back = conversation_hold(&world, 1, 0).kind;
        insults += kind_back == CHAT_INSULT;
        reverse_compliments += kind_back == CHAT_COMPLIMENT;
    }
    CHECK(compliments > 60);
    CHECK(insults > 60);
    CHECK(reverse_compliments < insults / 4);
    town_free();
}

/* Stories are what the teller lived through; lies are made up, and found
 * out more often from an awkward liar. */
static void test_stories_and_lies(void)
{
    town_init(2, NULL);
    ConversationWorld world = town_world(0);
    /* Nothing lived through: small talk instead. */
    CHECK_EQ_INT(conversation_hold_as(&world, 0, 1, CHAT_STORY).kind, CHAT_SMALL_TALK);
    experiences_add(&town.colonists[0].experiences, EXPERIENCE_KILLED_ANIMAL, ANIMAL_WOLF,
                    ANIMAL_MALE, NULL, 0);
    CHECK_EQ_INT(conversation_hold_as(&world, 0, 1, CHAT_STORY).kind, CHAT_STORY);
    const JournalEntry *told = journal_recent(&town.colonists[0].journal, 0);
    const JournalEntry *heard = journal_recent(&town.colonists[1].journal, 0);
    CHECK_EQ_INT(told->event, JOURNAL_TOLD_STORY);
    CHECK_EQ_INT(heard->event, JOURNAL_HEARD_STORY);
    CHECK_EQ_INT(heard->topic, STORY_TRUE);
    CHECK_EQ_INT(heard->what, EXPERIENCE_KILLED_ANIMAL);
    CHECK_EQ_INT(heard->animal, ANIMAL_WOLF + 1);
    CHECK_EQ_INT(heard->animal_sex, ANIMAL_MALE);
    CHECK_STR_EQ(heard->other, "Anna");
    CHECK(social_bond(&town.social, 1, 0)->memory_count > 0);
    /* A death seen is confided, with the name of who died. */
    experiences_init(&town.colonists[0].experiences);
    experiences_add(&town.colonists[0].experiences, EXPERIENCE_SAW_DEATH, 0, 0, "Gil", 0);
    conversation_hold_as(&world, 0, 1, CHAT_STORY);
    CHECK_STR_EQ(journal_recent(&town.colonists[1].journal, 0)->subject, "Gil");

    /* Lies: the liar always knows; the listener believes or not. */
    int found_out_awkward = 0, found_out_at_ease = 0;
    for (int i = 0; i < 400; i++) {
        ChatResult result = conversation_hold_as(&world, 0, 1, CHAT_LIE);
        CHECK_EQ_INT(result.kind, CHAT_LIE);
        const JournalEntry *lie = journal_recent(&town.colonists[0].journal, 0);
        const JournalEntry *listener = journal_recent(&town.colonists[1].journal, 0);
        CHECK_EQ_INT(lie->event, JOURNAL_LIED);
        CHECK_EQ_INT(lie->topic, STORY_LIE);
        CHECK(lie->what < LIE_TYPE_COUNT);
        CHECK_EQ_INT(lie->outcome, result.found_out);
        CHECK_EQ_INT(listener->event, result.found_out ? JOURNAL_CAUGHT_LIE : JOURNAL_HEARD_STORY);
        CHECK_EQ_INT(listener->what, lie->what);
        found_out_awkward += result.found_out;
    }
    Attributes smooth = town.colonists[0].attributes;
    attributes_set(&smooth, ATTRIBUTE_SOCIAL_EASE, ATTRIBUTE_POSITION_MAX);
    colonist_set_attributes(&town.colonists[0], &smooth);
    for (int i = 0; i < 400; i++) {
        found_out_at_ease += conversation_hold_as(&world, 0, 1, CHAT_LIE).found_out;
    }
    CHECK(found_out_awkward > 120 && found_out_awkward < 240); /* 45 %. */
    CHECK(found_out_at_ease < found_out_awkward / 2);
    /* Found out, liars are thought less of. */
    social_clear_slot(&town.social, 1);
    float before = town_opinion(1, 0, 0);
    bool caught = false;
    for (int i = 0; i < 20 && !caught; i++) {
        social_clear_slot(&town.social, 1);
        caught = conversation_hold_as(&world, 0, 1, CHAT_LIE).found_out;
    }
    REQUIRE(caught);
    CHECK(town_opinion(1, 0, 0) < before);
    town_free();
}

/* Small talk is about what is real: the last meal, the weather, what hurts. */
static void test_small_talk_topics(void)
{
    town_init(2, NULL);
    ConversationWorld world = town_world(0);
    mood_meal(&town.colonists[0].mood, ITEM_WILD_CARROTS, 100);
    Mood *mood = &town.colonists[0].mood;
    mood_set_states(mood, THOUGHT_BIT(THOUGHT_COLD));
    mood_advance(mood, TICKS_PER_HOUR);
    int topics[CHAT_TOPIC_COUNT] = {0};
    for (int i = 0; i < 400; i++) {
        conversation_hold_as(&world, 0, 1, CHAT_SMALL_TALK);
        const JournalEntry *entry = journal_recent(&town.colonists[0].journal, 0);
        const JournalEntry *other = journal_recent(&town.colonists[1].journal, 0);
        CHECK_EQ_INT(entry->event, JOURNAL_CHAT);
        CHECK(!entry->heard && other->heard);
        topics[entry->topic]++;
        if (entry->topic == CHAT_TOPIC_FOOD) {
            CHECK_EQ_INT(entry->what, ITEM_WILD_CARROTS);
        } else if (entry->topic == CHAT_TOPIC_WEATHER) {
            CHECK_EQ_INT(entry->what, WEATHER_RAIN);
        } else if (entry->topic == CHAT_TOPIC_COMPLAINT) {
            CHECK_EQ_INT(entry->what, THOUGHT_COLD);
        }
        CHECK(entry->topic == CHAT_TOPIC_NONE ||
              conversation_topic_key((ChatTopic)entry->topic, entry->what) != NULL);
    }
    CHECK(topics[CHAT_TOPIC_FOOD] > 50);
    CHECK(topics[CHAT_TOPIC_WEATHER] > 30);
    CHECK(topics[CHAT_TOPIC_COMPLAINT] > 50);
    CHECK(topics[CHAT_TOPIC_NONE] > 10);
    CHECK_EQ_INT(topics[CHAT_TOPIC_KNOWLEDGE], 0);
    /* Every food, sky and complaint has its phrase. */
    CHECK(conversation_topic_key(CHAT_TOPIC_WEATHER, JOURNAL_WEATHER_SNOW) != NULL);
    CHECK(conversation_topic_key(CHAT_TOPIC_FOOD, ITEM_WOOD) == NULL);
    CHECK(conversation_knowhow_key(KNOWLEDGE_FIRE) != NULL);
    CHECK(conversation_story_key(STORY_LIE, LIE_MAMMOTH) != NULL);
    CHECK(conversation_animal_key(ANIMAL_FOX, ANIMAL_FEMALE) != NULL);
    CHECK(conversation_animal_key(ANIMAL_SPECIES_COUNT, 0) == NULL);
    town_free();
}

/* Far ahead in a knowledge, a settler explains it: the listener learns and
 * thinks better of the teacher. */
static void test_explaining(void)
{
    town_init(2, NULL);
    ConversationWorld world = town_world(0);
    town.colonists[0].knowledge[KNOWLEDGE_STONE_KNAPPING] = 70.0;
    ChatResult result = conversation_hold_as(&world, 0, 1, CHAT_KNOWLEDGE);
    CHECK_EQ_INT(result.kind, CHAT_EXPLAIN);
    CHECK(town.colonists[1].knowledge[KNOWLEDGE_STONE_KNAPPING] > 0.0);
    const JournalEntry *entry = journal_recent(&town.colonists[1].journal, 0);
    CHECK_EQ_INT(entry->event, JOURNAL_LEARNED);
    CHECK_EQ_INT(entry->what, KNOWLEDGE_STONE_KNAPPING);
    CHECK_EQ_INT(journal_recent(&town.colonists[0].journal, 0)->event, JOURNAL_EXPLAINED);
    bool taught = false;
    const SocialBond *bond = social_bond_const(&town.social, 1, 0);
    for (int i = 0; i < bond->memory_count; i++) {
        taught |= bond->memories[i].type == SOCIAL_MEMORY_TAUGHT_ME;
    }
    CHECK(taught);
    /* Close in level: a chat about it. */
    town.colonists[1].knowledge[KNOWLEDGE_STONE_KNAPPING] = 60.0;
    CHECK_EQ_INT(conversation_hold_as(&world, 0, 1, CHAT_KNOWLEDGE).kind, CHAT_KNOWLEDGE);
    town_free();
}

/* Gossip: the listener thinks of the one talked about as the teller does. */
static void test_gossip(void)
{
    town_init(3, NULL);
    ConversationWorld world = town_world(0);
    /* Nobody to gossip about yet. */
    CHECK_EQ_INT(conversation_hold_as(&world, 0, 1, CHAT_GOSSIP).kind, CHAT_SMALL_TALK);
    for (int i = 0; i < 5; i++) {
        social_remember(&town.social, 0, 2, SOCIAL_MEMORY_INSULTED_ME, 1.0f, 0);
    }
    /* Not quite enough to talk about. */
    CHECK_EQ_INT(conversation_hold_as(&world, 0, 1, CHAT_GOSSIP).kind, CHAT_SMALL_TALK);
    social_remember(&town.social, 0, 2, SOCIAL_MEMORY_LIED_TO_ME, 1.0f, 0);
    CHECK_EQ_INT(conversation_hold_as(&world, 0, 1, CHAT_GOSSIP).kind, CHAT_GOSSIP);
    CHECK(town_opinion(1, 2, 0) < 0.0f);
    const JournalEntry *entry = journal_recent(&town.colonists[1].journal, 0);
    CHECK_EQ_INT(entry->event, JOURNAL_HEARD_GOSSIP);
    CHECK_EQ_INT(entry->outcome, 0);
    CHECK_STR_EQ(entry->subject, "Cyd");
    town_free();
}

/* Gossip comes back: the listener keeps what it heard, and may pass it on
 * to the settler it was about, who then thinks less of the gossip. */
static void test_gossip_comes_back(void)
{
    town_init(3, NULL);
    ConversationWorld world = town_world(0);
    for (int i = 0; i < 5; i++) {
        social_remember(&town.social, 0, 2, SOCIAL_MEMORY_INSULTED_ME, 1.0f, 0);
    }
    social_remember(&town.social, 0, 2, SOCIAL_MEMORY_LIED_TO_ME, 1.0f, 0);
    /* Nothing to pass on yet. */
    CHECK(social_tale_about(&town.social, 1, 2, 0) == NULL);
    CHECK_EQ_INT(conversation_hold_as(&world, 1, 2, CHAT_REPORT).kind, CHAT_SMALL_TALK);
    CHECK_EQ_INT(conversation_hold_as(&world, 0, 1, CHAT_GOSSIP).kind, CHAT_GOSSIP);
    const SocialTale *tale = social_tale_about(&town.social, 1, 2, 0);
    REQUIRE(tale != NULL);
    CHECK_EQ_INT(tale->teller, 0);
    CHECK_EQ_INT(tale->well, 0);
    /* Bo tells Cyd what Anna says behind Cyd's back. */
    float before = town_opinion(2, 0, 0);
    ChatResult result = conversation_hold_as(&world, 1, 2, CHAT_REPORT);
    CHECK_EQ_INT(result.kind, CHAT_REPORT);
    CHECK(town_opinion(2, 0, 0) < before);
    CHECK(mood_has(&town.colonists[2].mood, THOUGHT_INSULTED));
    const JournalEntry *entry = journal_recent(&town.colonists[2].journal, 0);
    CHECK_EQ_INT(entry->event, JOURNAL_HEARD_REPORT);
    CHECK_EQ_INT(entry->outcome, 0);
    CHECK_STR_EQ(entry->other, "Bo");
    CHECK_STR_EQ(entry->subject, "Anna");
    entry = journal_recent(&town.colonists[1].journal, 0);
    CHECK_EQ_INT(entry->event, JOURNAL_REPORTED);
    CHECK_STR_EQ(entry->other, "Cyd");
    CHECK_STR_EQ(entry->subject, "Anna");
    /* Passed on once. */
    CHECK(social_tale_about(&town.social, 1, 2, 0) == NULL);

    /* Kind words come back too. */
    social_hear_tale(&town.social, 1, 0, 2, true, 0);
    before = town_opinion(2, 0, 0);
    CHECK_EQ_INT(conversation_hold_as(&world, 1, 2, CHAT_REPORT).kind, CHAT_REPORT);
    CHECK(town_opinion(2, 0, 0) > before);
    CHECK_EQ_INT(journal_recent(&town.colonists[2].journal, 0)->outcome, 1);

    /* Tales are forgotten, and never about the teller itself. */
    social_hear_tale(&town.social, 1, 0, 2, false, 0);
    CHECK(social_tale_about(&town.social, 1, 2, SOCIAL_TALE_HOURS * HOUR) != NULL);
    CHECK(social_tale_about(&town.social, 1, 2, (SOCIAL_TALE_HOURS + 1) * HOUR) == NULL);
    social_hear_tale(&town.social, 1, 2, 2, false, 0);
    social_clear_slot(&town.social, 0);
    CHECK(social_tale_about(&town.social, 1, 2, 0) == NULL);

    /* Left to itself, a settler who likes Cyd better than the gossip tells. */
    social_remember(&town.social, 1, 2, SOCIAL_MEMORY_COMPLIMENTED_ME, 2.0f, 0);
    int reports = 0;
    for (int i = 0; i < 200; i++) {
        world.tick = (uint64_t)i * HOUR;
        social_hear_tale(&town.social, 1, 0, 2, false, world.tick);
        reports += conversation_hold(&world, 1, 2).kind == CHAT_REPORT;
    }
    CHECK(reports > 30);
    town_free();
}

static void set_trait(int slot, AttributeType type, float position)
{
    Attributes attributes = town.colonists[slot].attributes;
    attributes_set(&attributes, type, position);
    colonist_set_attributes(&town.colonists[slot], &attributes);
}

/* Settlers who dislike each other argue, and the unkind and bold come to
 * blows: bruises (never deadly), bad memories and moods on both sides. */
static void test_arguments_and_brawls(void)
{
    town_init(2, NULL);
    ConversationWorld world = town_world(0);
    ChatResult result = conversation_hold_as(&world, 0, 1, CHAT_ARGUMENT);
    CHECK_EQ_INT(result.kind, CHAT_ARGUMENT);
    CHECK(result.argued);
    CHECK(town_opinion(0, 1, 0) < 0.0f && town_opinion(1, 0, 0) < 0.0f);
    CHECK(mood_has(&town.colonists[0].mood, THOUGHT_ARGUMENT));
    CHECK(mood_has(&town.colonists[1].mood, THOUGHT_ARGUMENT));
    int event = journal_recent(&town.colonists[0].journal, 0)->event;
    CHECK(event == JOURNAL_ARGUED || event == JOURNAL_BRAWL);

    for (int slot = 0; slot < 2; slot++) {
        set_trait(slot, ATTRIBUTE_KINDNESS, -ATTRIBUTE_POSITION_MAX);
        set_trait(slot, ATTRIBUTE_CONFIDENCE, ATTRIBUTE_POSITION_MAX);
    }
    int brawls = 0;
    bool noted = false;
    for (int i = 0; i < 300; i++) {
        world.tick = (uint64_t)i * HOUR;
        result = conversation_hold_as(&world, 0, 1, CHAT_ARGUMENT);
        if (!result.brawl) {
            continue;
        }
        brawls++;
        const SocialBond *bond = social_bond(&town.social, 1, 0);
        bool fought = false;
        for (int m = 0; m < bond->memory_count; m++) {
            fought |= bond->memories[m].type == SOCIAL_MEMORY_FOUGHT &&
                      bond->memories[m].hour == world.tick / HOUR;
        }
        CHECK(fought);
        CHECK(mood_has(&town.colonists[0].mood, THOUGHT_FOUGHT));
        CHECK_EQ_INT(journal_recent(&town.colonists[1].journal, 0)->event, JOURNAL_BRAWL);
        HurtNote notes[HEALTH_NOTES_MAX];
        int count = health_take_notes(&town.colonists[1].health, notes);
        for (int n = 0; n < count; n++) {
            noted |= notes[n].cause == HURT_BRAWL && notes[n].type == INJURY_BRUISE;
        }
        health_take_notes(&town.colonists[0].health, NULL);
        CHECK(!health_is_dead(&town.colonists[0].health));
        CHECK(!health_is_dead(&town.colonists[1].health));
        if (i % 2 == 0) { /* Bruises heal (or the settlers would be too sore to fight). */
            health_update(&town.colonists[0].health, 72.0f);
            health_update(&town.colonists[1].health, 72.0f);
        }
    }
    CHECK(brawls > 30);
    CHECK(noted);

    /* An insult may be answered back: an argument. */
    social_clear_slot(&town.social, 0);
    int answered = 0;
    for (int i = 0; i < 100; i++) {
        world.tick = (uint64_t)(400 + i) * HOUR;
        answered += conversation_hold_as(&world, 0, 1, CHAT_INSULT).argued;
    }
    CHECK(answered > 20);
    /* Kind settlers seldom argue on their own. */
    social_clear_slot(&town.social, 0);
    for (int i = 0; i < 6; i++) {
        social_remember(&town.social, 0, 1, SOCIAL_MEMORY_INSULTED_ME, 1.0f, world.tick);
    }
    int unkind = 0, kind = 0;
    for (int i = 0; i < 300; i++) {
        unkind += conversation_hold(&world, 0, 1).kind == CHAT_ARGUMENT;
    }
    set_trait(0, ATTRIBUTE_KINDNESS, ATTRIBUTE_POSITION_MAX);
    for (int i = 0; i < 300; i++) {
        kind += conversation_hold(&world, 0, 1).kind == CHAT_ARGUMENT;
    }
    CHECK(unkind > 3 * kind + 10);
    town_free();
}

/* A settler not unkind apologizes for its wrongs: forgiven, half of them
 * are mended; refused, it will not ask again until it wrongs the other anew. */
static void test_apologies(void)
{
    town_init(2, NULL);
    ConversationWorld world = town_world(0);
    CHECK(!social_owes_apology(&town.social, 1, 0, 0));
    social_remember(&town.social, 1, 0, SOCIAL_MEMORY_INSULTED_ME, 1.0f, 0);
    CHECK(social_owes_apology(&town.social, 1, 0, 0));
    CHECK(!social_owes_apology(&town.social, 0, 1, 0));
    CHECK_NEAR(social_wrongs(&town.social, 1, 0, 0),
               -social_memory_def(SOCIAL_MEMORY_INSULTED_ME)->value, 1e-3);

    /* A kind settler who likes the other forgives more often than an
     * abrasive one who holds a grudge. */
    int forgiven[2] = {0, 0}, refused[2] = {0, 0};
    for (int round = 0; round < 2; round++) {
        social_clear_slot(&town.social, 0);
        set_trait(1, ATTRIBUTE_KINDNESS,
                  round == 0 ? ATTRIBUTE_POSITION_MAX : -ATTRIBUTE_POSITION_MAX);
        if (round == 0) {
            social_remember(&town.social, 1, 0, SOCIAL_MEMORY_JOURNEY, 2.0f, 0);
        } else {
            social_remember(&town.social, 1, 0, SOCIAL_MEMORY_FOUGHT, 3.0f, 0);
        }
        for (int i = 0; i < 60; i++) {
            world.tick = (uint64_t)(round * 100 + i) * HOUR;
            social_remember(&town.social, 1, 0, SOCIAL_MEMORY_INSULTED_ME, 1.0f, world.tick);
            CHECK(social_owes_apology(&town.social, 1, 0, world.tick));
            float wrongs = social_wrongs(&town.social, 1, 0, world.tick);
            ChatResult result = conversation_hold_as(&world, 0, 1, CHAT_APOLOGY);
            CHECK_EQ_INT(result.kind, CHAT_APOLOGY);
            const JournalEntry *entry = journal_recent(&town.colonists[1].journal, 0);
            CHECK_EQ_INT(entry->event, JOURNAL_FORGAVE);
            CHECK_EQ_INT(entry->outcome, result.forgiven);
            CHECK_EQ_INT(journal_recent(&town.colonists[0].journal, 0)->event, JOURNAL_APOLOGIZED);
            if (result.forgiven) {
                forgiven[round]++;
                CHECK_NEAR(social_wrongs(&town.social, 1, 0, world.tick),
                           wrongs * (1.0f - SOCIAL_APOLOGY_FORGIVES), 1e-2);
                CHECK(mood_has(&town.colonists[1].mood, THOUGHT_APOLOGIZED_TO));
            } else {
                refused[round]++;
            }
            /* Not again until the next wrong. */
            CHECK(!social_owes_apology(&town.social, 1, 0, world.tick));
        }
    }
    CHECK(forgiven[0] > 3 * refused[0]);
    CHECK(refused[1] > forgiven[1]);

    /* Left to itself, a kind settler apologizes; one who owes nothing does not. */
    social_clear_slot(&town.social, 0);
    set_trait(0, ATTRIBUTE_KINDNESS, ATTRIBUTE_POSITION_MAX);
    int apologies = 0;
    for (int i = 0; i < 100; i++) {
        world.tick = (uint64_t)(300 + i) * HOUR;
        bool owed = social_owes_apology(&town.social, 1, 0, world.tick);
        if (conversation_hold(&world, 0, 1).kind == CHAT_APOLOGY) {
            CHECK(owed);
        }
    }
    for (int i = 0; i < 100; i++) {
        world.tick = (uint64_t)(400 + i) * HOUR;
        social_remember(&town.social, 1, 0, SOCIAL_MEMORY_INSULTED_ME, 1.0f, world.tick);
        apologies += conversation_hold(&world, 0, 1).kind == CHAT_APOLOGY;
    }
    CHECK(apologies > 20);
    town_free();
}

/* A friend close by is good company, a rival unwelcome; asleep, far off or
 * on another level, nobody counts. */
static void test_company_of_friends_and_rivals(void)
{
    town_init(3, NULL);
    town.social.bonds[0][1].level = RELATION_FRIEND;
    town.social.bonds[0][2].level = RELATION_RIVAL;
    social_company_update(&town.social, town.colonists, &town.pool);
    CHECK(town.colonists[0].social_states ==
          (THOUGHT_BIT(THOUGHT_GOOD_COMPANY) | THOUGHT_BIT(THOUGHT_UNWELCOME_COMPANY)));
    CHECK(town.colonists[1].social_states == 0);
    town.colonists[2].x = 20;
    social_company_update(&town.social, town.colonists, &town.pool);
    CHECK(town.colonists[0].social_states == THOUGHT_BIT(THOUGHT_GOOD_COMPANY));
    town.colonists[1].z = 1;
    social_company_update(&town.social, town.colonists, &town.pool);
    CHECK(town.colonists[0].social_states == 0);
    town.colonists[1].z = 0;
    town.colonists[1].task = TASK_SLEEP;
    social_company_update(&town.social, town.colonists, &town.pool);
    CHECK(town.colonists[0].social_states == 0);
    town.colonists[1].task = TASK_IDLE;
    town.colonists[0].task = TASK_SLEEP;
    social_company_update(&town.social, town.colonists, &town.pool);
    CHECK(town.colonists[0].social_states == 0);
    town_free();
}

/* In a game, friends side by side feel each other's company. */
static void test_good_company_in_a_game(void)
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
    EntityId a_id = game_spawn_colonist(&game, 10, 10, 0);
    EntityId b_id = game_spawn_colonist(&game, 11, 10, 0);
    Colonist *a = game_colonist(&game, a_id), *b = game_colonist(&game, b_id);
    REQUIRE(a != NULL && b != NULL);
    make_ordinary(a);
    make_ordinary(b);
    int sa = (int)a_id.index, sb = (int)b_id.index;
    social_remember(&game.social, sa, sb, SOCIAL_MEMORY_JOURNEY, 3.0f, 0);
    social_remember(&game.social, sb, sa, SOCIAL_MEMORY_JOURNEY, 3.0f, 0);
    bool felt = false;
    for (int tick = 0; tick < 2 * TICKS_PER_HOUR && !felt; tick++) {
        for (int need = 0; need < NEED_COUNT; need++) {
            a->needs[need] = b->needs[need] = 1.0f;
        }
        game_tick(&game);
        felt = mood_has(&a->mood, THOUGHT_GOOD_COMPANY);
    }
    CHECK(felt);
    game_shutdown(&game);
}

/* Chats fill the company need; friends chat more than rivals. */
static void test_company_and_chances(void)
{
    town_init(2, NULL);
    ConversationWorld world = town_world(0);
    town.colonists[0].company = 0.1f;
    town.colonists[1].company = 0.1f;
    float lonely = conversation_chance(&world, 0, 1);
    conversation_hold_as(&world, 0, 1, CHAT_SMALL_TALK);
    CHECK(town.colonists[0].company >= 0.1f + CONVERSATION_COMPANY_AWKWARD - 1e-6f);
    CHECK(town.colonists[1].company >= 0.1f + CONVERSATION_COMPANY_AWKWARD - 1e-6f);
    town.colonists[0].company = town.colonists[1].company = 1.0f;
    social_clear_slot(&town.social, 0);
    float ordinary = conversation_chance(&world, 0, 1);
    CHECK(lonely > ordinary);
    social_remember(&town.social, 0, 1, SOCIAL_MEMORY_JOURNEY, 3.0f, 0);
    social_remember(&town.social, 1, 0, SOCIAL_MEMORY_JOURNEY, 3.0f, 0);
    float friends = conversation_chance(&world, 0, 1);
    social_clear_slot(&town.social, 0);
    social_remember(&town.social, 0, 1, SOCIAL_MEMORY_INSULTED_ME, 5.0f, 0);
    social_remember(&town.social, 1, 0, SOCIAL_MEMORY_INSULTED_ME, 5.0f, 0);
    float rivals = conversation_chance(&world, 0, 1);
    CHECK(friends > ordinary && ordinary > rivals);
    /* Sociable settlers chat more. */
    social_clear_slot(&town.social, 0);
    Attributes sociable = town.colonists[0].attributes;
    attributes_set(&sociable, ATTRIBUTE_SOCIABILITY, ATTRIBUTE_POSITION_MAX);
    colonist_set_attributes(&town.colonists[0], &sociable);
    colonist_set_attributes(&town.colonists[1], &sociable);
    CHECK(conversation_chance(&world, 0, 1) > ordinary * 1.5f);
    town_free();
}

/* In a game: company runs out alone, the settler feels lonely, and goes
 * looking for someone when idle; chats keep a pair in company. */
static void test_loneliness_in_a_game(void)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    REQUIRE(game_init(&game, &params));
    for (size_t i = 0; i < map_level_tile_count(&game.map); i++) {
        game.map.terrain[i] = TERRAIN_SOIL;
        game.map.water[i] = 0;
        game.map.rock[i] = ROCK_NONE;
        memset(&game.map.plants[i], 0, sizeof(game.map.plants[i]));
    }
    map_topology_changed(&game.map);
    EntityId loner_id = game_spawn_colonist(&game, 5, 5, 0);
    EntityId other_id = game_spawn_colonist(&game, 30, 30, 0);
    Colonist *loner = game_colonist(&game, loner_id), *other = game_colonist(&game, other_id);
    REQUIRE(loner != NULL && other != NULL);
    make_ordinary(loner);
    make_ordinary(other);
    for (int need = 0; need < NEED_COUNT; need++) {
        loner->needs[need] = other->needs[need] = 1.0f;
    }
    loner->company = COLONIST_LONELY_BELOW - 0.05f;
    float start = loner->company;
    int closest = 1000;
    bool felt_lonely = false;
    for (int tick = 0; tick < 2 * TICKS_PER_HOUR; tick++) {
        game_tick(&game);
        loner->needs[NEED_REST] = other->needs[NEED_REST] = 1.0f; /* Nobody sleeps. */
        felt_lonely |= mood_has(&loner->mood, THOUGHT_LONELY);
        int dx = loner->x - other->x, dy = loner->y - other->y;
        int distance = dx * dx + dy * dy;
        closest = distance < closest ? distance : closest;
    }
    CHECK(felt_lonely);
    CHECK(closest <= 8); /* Walked over to the other. */
    (void)start;
    game_shutdown(&game);
}

/* Two settlers idle on a flat bare world, `distance` tiles apart, their
 * company held at `company`, `level` what they are to each other: returns
 * the closest they came (squared) over `hours`; `together`, the share of
 * the time within chatting range. */
static int idle_pair(int distance, float company, RelationLevel level, int hours, float *together)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 8;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = 0;
    if (!game_init(&game, &params)) {
        CHECK(false);
        return 0;
    }
    for (size_t i = 0; i < map_level_tile_count(&game.map); i++) {
        game.map.terrain[i] = TERRAIN_SOIL;
        game.map.water[i] = 0;
        game.map.rock[i] = ROCK_NONE;
        memset(&game.map.plants[i], 0, sizeof(game.map.plants[i]));
    }
    map_topology_changed(&game.map);
    EntityId a_id = game_spawn_colonist(&game, 10, 20, 0);
    EntityId b_id = game_spawn_colonist(&game, 10 + distance, 20, 0);
    Colonist *a = game_colonist(&game, a_id), *b = game_colonist(&game, b_id);
    if (a == NULL || b == NULL) {
        CHECK(false);
        game_shutdown(&game);
        return 0;
    }
    make_ordinary(a);
    make_ordinary(b);
    game_debug_relation(&game, a_id, b_id, level);
    int closest = 1 << 30, near = 0, samples = 0;
    for (int tick = 0; tick < hours * TICKS_PER_HOUR; tick++) {
        for (int need = 0; need < NEED_COUNT; need++) {
            a->needs[need] = b->needs[need] = 1.0f;
        }
        a->company = b->company = company;
        a->chat_ticks = b->chat_ticks = 0; /* No chat stops them. */
        game_tick(&game);
        int dx = a->x - b->x, dy = a->y - b->y;
        int squared = dx * dx + dy * dy;
        closest = squared < closest ? squared : closest;
        if (tick % 100 == 0) {
            samples++;
            near += squared <= COLONIST_COMPANY_NEAR * COLONIST_COMPANY_NEAR;
        }
    }
    if (together != NULL) {
        *together = (float)near / (float)samples;
    }
    game_shutdown(&game);
    return closest;
}

/* Idle settlers go over to the others the more as company runs low, the
 * ones they like, never rivals; in company already, they stay. */
static void test_settlers_seek_company(void)
{
    CHECK_NEAR(colonist_seek_company_chance(1.0f), 0.0f, 1e-6);
    CHECK_NEAR(colonist_seek_company_chance(COLONIST_SEEK_COMPANY_FROM), 0.0f, 1e-6);
    CHECK_NEAR(colonist_seek_company_chance(COLONIST_LONELY_BELOW - 0.01f), 1.0f, 1e-6);
    float middle = 0.5f * (COLONIST_SEEK_COMPANY_FROM + COLONIST_LONELY_BELOW);
    CHECK_NEAR(colonist_seek_company_chance(middle), 0.5f, 1e-4);

    /* Far apart: a settler that misses company walks over; one that has
     * plenty does not, nor one that would only find an enemy. */
    CHECK(idle_pair(30, 0.5f, RELATION_ACQUAINTANCE, 2, NULL) <= 9);
    CHECK(idle_pair(30, 1.0f, RELATION_ACQUAINTANCE, 2, NULL) > 100);
    CHECK(idle_pair(30, 0.1f, RELATION_ENEMY, 2, NULL) > 100);
    /* Side by side: friends who miss company stay together far more than
     * settlers who have plenty. */
    float needy = 0.0f, content = 0.0f;
    idle_pair(2, 0.4f, RELATION_FRIEND, 3, &needy);
    idle_pair(2, 1.0f, RELATION_FRIEND, 3, &content);
    CHECK(needy > 0.5f);
    CHECK(needy > content + 0.3f);
}

/* Company runs out when alone and awake. */
static void test_company_runs_out(void)
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
    EntityId id = game_spawn_colonist(&game, 10, 10, 0);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    make_ordinary(colonist);
    for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
        colonist->needs[NEED_REST] = 1.0f;
        game_tick(&game);
    }
    CHECK_NEAR(colonist->company, 1.0f - TICKS_PER_HOUR * COLONIST_COMPANY_DECAY_PER_TICK, 0.01f);
    game_shutdown(&game);
}

/* Debug tools: opinions and relationships set, family ties cycled, any
 * kind of chat forced (what it needs made up), discord sown and forgotten. */
static void test_debug_tools(void)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 4;
    params.world.width = 32;
    params.world.height = 32;
    params.colonist_count = 0;
    REQUIRE(game_init(&game, &params));
    EntityId ids[3];
    for (int i = 0; i < 3; i++) {
        ids[i] = game_spawn_colonist(&game, 5 + 10 * i, 5, 0); /* Far from each other. */
        REQUIRE(game_colonist(&game, ids[i]) != NULL);
        make_ordinary(game_colonist(&game, ids[i]));
    }
    int a = (int)ids[0].index, b = (int)ids[1].index, c = (int)ids[2].index;
    uint64_t tick = game.clock.tick;
    CHECK(!game_debug_relation(&game, ids[0], ids[0], RELATION_ENEMY));
    REQUIRE(game_debug_relation(&game, ids[0], ids[1], RELATION_ENEMY));
    CHECK_NEAR(social_opinion(&game.social, game.colonists, a, b, tick), GAME_DEBUG_ENEMY_OPINION,
               1e-3);
    CHECK_NEAR(social_opinion(&game.social, game.colonists, b, a, tick), GAME_DEBUG_ENEMY_OPINION,
               1e-3);
    CHECK_EQ_INT(game.social.bonds[a][b].level, RELATION_ENEMY);
    CHECK_EQ_INT(journal_recent(&game.colonists[a].journal, 0)->event, JOURNAL_RELATION);
    REQUIRE(game_debug_opinion(&game, ids[0], ids[1], 1));
    CHECK_NEAR(social_opinion(&game.social, game.colonists, a, b, tick),
               GAME_DEBUG_ENEMY_OPINION + GAME_DEBUG_OPINION_STEP, 1e-3);
    REQUIRE(game_debug_relation(&game, ids[0], ids[1], RELATION_CLOSE_FRIEND));
    CHECK_EQ_INT(game.social.bonds[b][a].level, RELATION_CLOSE_FRIEND);

    /* Family ties, round and round. */
    REQUIRE(game_debug_family(&game, ids[0], ids[1]));
    CHECK_EQ_INT(game.social.bonds[a][b].family, FAMILY_PARENT);
    CHECK_EQ_INT(game.social.bonds[b][a].family, FAMILY_CHILD);
    for (int i = 1; i < FAMILY_TIE_COUNT; i++) {
        game_debug_family(&game, ids[0], ids[1]);
    }
    CHECK_EQ_INT(game.social.bonds[a][b].family, FAMILY_NONE);

    /* Every kind of chat, wherever they are. */
    CHECK(!game_debug_chat(&game, ids[0], ids[1], GAME_DEBUG_CHAT_BRAWL + 1, NULL));
    CHECK(!game_debug_chat(&game, ids[0], ENTITY_NONE, CHAT_SMALL_TALK, NULL));
    for (int kind = GAME_DEBUG_CHAT_ANY; kind <= GAME_DEBUG_CHAT_BRAWL; kind++) {
        ChatResult result;
        uint32_t before = game.chat_counts.kinds[CHAT_SMALL_TALK];
        REQUIRE(game_debug_chat(&game, ids[0], ids[1], kind, &result));
        if (kind == GAME_DEBUG_CHAT_BRAWL) {
            CHECK_EQ_INT(result.kind, CHAT_ARGUMENT);
            CHECK(result.brawl);
            CHECK_EQ_INT(journal_recent(&game.colonists[b].journal, 0)->event, JOURNAL_BRAWL);
        } else if (kind == CHAT_KNOWLEDGE || kind == CHAT_EXPLAIN) {
            CHECK(result.kind == CHAT_KNOWLEDGE || result.kind == CHAT_EXPLAIN);
        } else if (kind != GAME_DEBUG_CHAT_ANY) {
            CHECK_EQ_INT(result.kind, kind); /* Gossip, a tale, a story: made up. */
        }
        CHECK(kind == CHAT_SMALL_TALK || kind == GAME_DEBUG_CHAT_ANY ||
              game.chat_counts.kinds[CHAT_SMALL_TALK] == before);
        CHECK_EQ_INT(game.colonists[a].chat_ticks, CONVERSATION_TICKS);
    }
    CHECK_EQ_INT(game.chat_counts.brawls >= 1, 1);
    /* The tale passed on came from the third settler. */
    CHECK(social_tale_about(&game.social, a, b, tick) == NULL);

    /* Discord: everybody against everybody; then forgotten. */
    game_debug_discord(&game);
    for (int who = 0; who < 3; who++) {
        for (int whom = 0; whom < 3; whom++) {
            if (who != whom) {
                CHECK(game.social.bonds[who][whom].level <= RELATION_RIVAL);
            }
        }
    }
    game_debug_forget_social(&game);
    CHECK(social_opinion(&game.social, game.colonists, c, a, tick) > SOCIAL_RIVAL_TO);
    for (int i = 0; i < game.social.bonds[c][a].memory_count; i++) {
        CHECK(game.social.bonds[c][a].memories[i].type != SOCIAL_MEMORY_DEBUG);
    }
    game_shutdown(&game);
}

/* The two clans of a feud: friends within, enemies across, half abrasive. */
static void test_feud(void)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 4;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 4;
    REQUIRE(game_init(&game, &params));
    game_debug_feud(&game);
    CHECK_EQ_INT(game.social.bonds[0][2].level, RELATION_FRIEND);
    CHECK_EQ_INT(game.social.bonds[1][3].level, RELATION_FRIEND);
    CHECK_EQ_INT(game.social.bonds[0][1].level, RELATION_ENEMY);
    CHECK_EQ_INT(game.social.bonds[3][2].level, RELATION_ENEMY);
    CHECK(attribute_position(&game.colonists[0].attributes, ATTRIBUTE_KINDNESS) < -0.5f);
    CHECK(attribute_position(&game.colonists[2].attributes, ATTRIBUTE_KINDNESS) > 0.5f);
    /* A situation to start from: no journal note. */
    CHECK_EQ_INT(game.colonists[0].journal.count, 0);
    game_shutdown(&game);
}

/* The same seed gives the same social life. */
static void test_social_life_is_deterministic(void)
{
    static Game a, b;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 9;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = 6;
    REQUIRE(game_init(&a, &params));
    REQUIRE(game_init(&b, &params));
    int chats = 0;
    for (int tick = 0; tick < 6 * TICKS_PER_HOUR; tick++) {
        game_tick(&a);
        game_tick(&b);
    }
    for (uint32_t slot = 0; slot < a.colonist_pool.high_water; slot++) {
        chats += a.colonists[slot].journal.count;
    }
    CHECK(chats > 0);
    CHECK(game_hash(&a) == game_hash(&b));
    uint64_t before = game_hash(&a);
    social_remember(&a.social, 0, 1, SOCIAL_MEMORY_COMPLIMENTED_ME, 1.0f, a.clock.tick);
    CHECK(game_hash(&a) != before);
    game_shutdown(&a);
    game_shutdown(&b);
}

int main(void)
{
    RUN_TEST(test_memories_fade_over_years);
    RUN_TEST(test_memories_stack_and_make_room);
    RUN_TEST(test_opinion_shares);
    RUN_TEST(test_relationships_change_with_slack);
    RUN_TEST(test_slots_are_cleared);
    RUN_TEST(test_families_follow_ages);
    RUN_TEST(test_starting_settlers_like_each_other);
    RUN_TEST(test_grief_and_killings);
    RUN_TEST(test_experiences_ring);
    RUN_TEST(test_compliments_and_insults);
    RUN_TEST(test_stories_and_lies);
    RUN_TEST(test_small_talk_topics);
    RUN_TEST(test_explaining);
    RUN_TEST(test_gossip);
    RUN_TEST(test_gossip_comes_back);
    RUN_TEST(test_arguments_and_brawls);
    RUN_TEST(test_apologies);
    RUN_TEST(test_company_of_friends_and_rivals);
    RUN_TEST(test_good_company_in_a_game);
    RUN_TEST(test_company_and_chances);
    RUN_TEST(test_loneliness_in_a_game);
    RUN_TEST(test_company_runs_out);
    RUN_TEST(test_settlers_seek_company);
    RUN_TEST(test_debug_tools);
    RUN_TEST(test_feud);
    RUN_TEST(test_social_life_is_deterministic);
    return test_summary();
}
