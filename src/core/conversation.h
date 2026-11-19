/*
 * Conversation: settlers close to each other chat now and then, learn a
 * little of what the other knows (docs/KNOWLEDGE.md §1.5), and make up their
 * minds about each other (docs/SOCIAL.md §6).
 *
 * Every CONVERSATION_CHECK_TICKS, each pair of settlers free to chat
 * (colonist_can_chat()) within CONVERSATION_RANGE tiles starts a chat with
 * the chance of CONVERSATION_CHATS_PER_HOUR chats per hour spent together,
 * more for sociable or confident settlers, friends and the lonely, less for
 * those who dislike each other. A chat lasts CONVERSATION_TICKS.
 *
 * The first of the pair leads the chat, which is one of:
 * - about knowledge (CONVERSATION_KNOWLEDGE_CHANCE): each brings up one of
 *   their own, weighted by level (twice as much for one practised lately),
 *   and the other learns for the length of the chat at
 *   KNOWLEDGE_CONVERSATION_RATE × the speaker's level / 100, never past the
 *   speaker's level nor KNOWLEDGE_CONVERSATION_CAP. Far ahead of the other
 *   (CONVERSATION_EXPLAIN_GAP), the leader explains it: a good memory;
 * - a story the leader lived through (core/experience.h), or a made-up one:
 *   unkind, self-assured settlers lie more, and the more at ease they are,
 *   the better their lies pass; a lie found out is a bad memory;
 * - a compliment (kind settlers, to those they like) or an insult
 *   (abrasive ones, to those they dislike);
 * - gossip about a settler the leader likes or dislikes a lot, which the
 *   listener keeps (core/social.h SocialTale) and may pass on to that settler
 *   later: learning that someone speaks ill of you behind your back hurts;
 * - an argument, between settlers who dislike each other (or an insult
 *   answered back), that may turn into a brawl: bruises, and bad memories
 *   on both sides (never deadly);
 * - an apology, from a settler not unkind who wronged the other (an insult,
 *   a lie found out, an argument, blows): accepted, it mends half of it;
 * - small talk: a recent meal, the weather, or what the leader suffers from.
 * Chats go well or badly with social ease and opinion (pleasant, neutral,
 * awkward): each a memory of the other. Every chat fills the company need;
 * both note it in their journal. The randomness comes from the social stream
 * (Social.rng).
 */
#ifndef COLONY_CORE_CONVERSATION_H
#define COLONY_CORE_CONVERSATION_H

#include "core/colonist.h"
#include "core/journal.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/social.h"

#include <stdint.h>

#define CONVERSATION_CHECK_TICKS 250
#define CONVERSATION_RANGE 3
#define CONVERSATION_CHATS_PER_HOUR 2.0f
#define CONVERSATION_TICKS 250 /* 6 in-game minutes. */
/* Twice the chats of old, half of them about knowledge: learning in chats
 * goes as fast as before. */
#define CONVERSATION_KNOWLEDGE_CHANCE 0.2f
/* A knowledge practised within this many ticks counts as "lately". */
#define CONVERSATION_RECENT_TICKS TICKS_PER_DAY
#define CONVERSATION_RECENT_WEIGHT 2.0
/* Levels ahead of the listener from which a speaker explains a knowledge. */
#define CONVERSATION_EXPLAIN_GAP 20.0
/* Company a chat brings, by how it went; to the one insulted. */
#define CONVERSATION_COMPANY_PLEASANT 0.4f
#define CONVERSATION_COMPANY_NEUTRAL 0.35f
#define CONVERSATION_COMPANY_AWKWARD 0.2f
#define CONVERSATION_COMPANY_INSULTED 0.05f
/* A settler gossips about another it thinks at least this well or ill of. */
#define CONVERSATION_GOSSIP_OPINION 40.0f
/* Brawls: bruises of this many points (before the damage factor) to each. */
#define CONVERSATION_BRAWL_DAMAGE_MIN 2.0f
#define CONVERSATION_BRAWL_DAMAGE_MAX 6.0f

/* What a chat turned into (see above). */
typedef enum ChatKind {
    CHAT_SMALL_TALK,
    CHAT_KNOWLEDGE,
    CHAT_EXPLAIN,
    CHAT_STORY,
    CHAT_LIE,
    CHAT_COMPLIMENT,
    CHAT_INSULT,
    CHAT_GOSSIP,
    CHAT_REPORT,   /* Passing on what a third settler said of the listener. */
    CHAT_ARGUMENT, /* Maybe a brawl. */
    CHAT_APOLOGY,
    CHAT_KIND_COUNT
} ChatKind;

/* Made-up stories (feats that never were). */
typedef enum LieType {
    LIE_CAVE_BEAR,
    LIE_WOLF_BARE_HANDS,
    LIE_AUROCHS,
    LIE_OUTRAN_STAG,
    LIE_THREE_BOARS,
    LIE_MAMMOTH,
    LIE_FLINT_CAVE,
    LIE_WINTER_ALONE,
    LIE_TYPE_COUNT
} LieType;

typedef enum ComplimentType {
    COMPLIMENT_GENERIC,
    COMPLIMENT_LOOKS, /* To a striking settler. */
    COMPLIMENT_SKILL, /* The other's best knowledge (in JournalEntry.topic). */
    COMPLIMENT_TYPE_COUNT
} ComplimentType;

typedef enum InsultType {
    INSULT_GENERIC,
    INSULT_LOOKS, /* To a plain settler. */
    INSULT_LAZY,  /* To a slow worker. */
    INSULT_SMELL, /* To a soaked one. */
    INSULT_TYPE_COUNT
} InsultType;

/* Chats held so far, by what they turned into: for the curious (the debug
 * menu, bench_social). Not part of the simulation state. */
typedef struct ConversationCounts {
    uint32_t kinds[CHAT_KIND_COUNT];
    uint32_t lies_found_out;
    uint32_t answered_back; /* Insults that turned into an argument. */
    uint32_t brawls;
    uint32_t forgiven; /* Apologies accepted... */
    uint32_t refused;  /* ...and refused. */
} ConversationCounts;

/* What the chats see of the world. */
typedef struct ConversationWorld {
    Colonist *colonists; /* Indexed by slot. */
    const Pool *pool;
    Social *social; /* Opinions, memories, and the random stream. */
    uint64_t tick;
    int weather;                /* WeatherType, JOURNAL_WEATHER_SNOW while it snows, -1 unknown. */
    ConversationCounts *counts; /* Counted in when not NULL. */
} ConversationWorld;

/* How a chat went, for tests and the curious. */
typedef struct ChatResult {
    ChatKind kind;
    ChatQuality quality;
    bool found_out; /* A lie that did not pass. */
    bool argued;    /* An argument (a chat that was one, or an insult answered back). */
    bool brawl;     /* The argument came to blows. */
    bool forgiven;  /* An apology accepted. */
} ChatResult;

/* Whether the colonist may start a chat now: free to chat, not chatting. */
bool conversation_available(const Colonist *colonist);

/* Whether two colonists are close enough to chat. */
bool conversation_in_range(const Colonist *a, const Colonist *b);

/* A knowledge the speaker brings up (see above), KNOWLEDGE_NONE when it has
 * none of at least KNOWLEDGE_DISCOVERED_LEVEL. */
KnowledgeType conversation_pick_topic(const Colonist *speaker, uint64_t tick, Rng *rng);

/* The settler `a` would gossip about with `b`: the one it thinks most of,
 * or least, beyond CONVERSATION_GOSSIP_OPINION either way; -1 when none. */
int conversation_gossip_subject(const ConversationWorld *world, int a, int b);

/* Chance of a chat per check for two settlers (slots `a`, `b`) together. */
float conversation_chance(const ConversationWorld *world, int a, int b);

/* A chat led by `a` with `b` (slots), what it turns into drawn as above.
 * Ids of each other are kept for the player. */
ChatResult conversation_hold(const ConversationWorld *world, int a, int b);

/* The same, as the kind given (when it cannot be, small talk; an apology
 * always is one, whatever the wrongs). */
ChatResult conversation_hold_as(const ConversationWorld *world, int a, int b, ChatKind kind);

/* Debug: an argument between `a` and `b` that comes to blows, whatever they
 * think of each other (when neither is down). */
ChatResult conversation_brawl(const ConversationWorld *world, int a, int b);

/* The check of every CONVERSATION_CHECK_TICKS over the living colonists.
 * Returns the number of chats started. */
int conversations_update(const ConversationWorld *world);

/* Translation keys of the phrases chats are told with (docs/SOCIAL.md §7):
 * what a chat was about ("wild carrots", "the cold"), a knowledge as know-how
 * ("how to make fire"), a story ("killing {0}"), an animal of a story ("a
 * wolf"). NULL when there is none. */
const char *conversation_topic_key(ChatTopic topic, int what);
const char *conversation_knowhow_key(KnowledgeType knowledge);
const char *conversation_story_key(StoryKind kind, int what);
const char *conversation_animal_key(int species, int sex);

#endif /* COLONY_CORE_CONVERSATION_H */
