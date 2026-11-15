/*
 * Conversation: settlers close to each other chat now and then, and learn a
 * little of what the other knows (docs/KNOWLEDGE.md §1.5).
 *
 * Every CONVERSATION_CHECK_TICKS, each pair of settlers free to chat
 * (colonist_can_chat()) within CONVERSATION_RANGE tiles starts a chat with
 * the chance of CONVERSATION_CHATS_PER_HOUR chats per hour spent together.
 * A chat lasts CONVERSATION_TICKS. Some are about a knowledge: each settler
 * brings up one of their own, weighted by level (twice as much for one
 * practised lately: settlers talk about what they do), and the other learns
 * for the length of the chat at KNOWLEDGE_CONVERSATION_RATE × the speaker's
 * level / 100, never past the speaker's level nor KNOWLEDGE_CONVERSATION_CAP.
 * The randomness comes from the game's seeded generator.
 */
#ifndef COLONY_CORE_CONVERSATION_H
#define COLONY_CORE_CONVERSATION_H

#include "core/colonist.h"
#include "core/pool.h"
#include "core/rng.h"

#include <stdint.h>

#define CONVERSATION_CHECK_TICKS 250
#define CONVERSATION_RANGE 3
#define CONVERSATION_CHATS_PER_HOUR 1.0f
#define CONVERSATION_TICKS 250 /* 6 in-game minutes. */
#define CONVERSATION_KNOWLEDGE_CHANCE 0.4f
/* A knowledge practised within this many ticks counts as "lately". */
#define CONVERSATION_RECENT_TICKS TICKS_PER_DAY
#define CONVERSATION_RECENT_WEIGHT 2.0

/* Whether the colonist may start a chat now: free to chat, not chatting. */
bool conversation_available(const Colonist *colonist);

/* Whether two colonists are close enough to chat. */
bool conversation_in_range(const Colonist *a, const Colonist *b);

/* A knowledge the speaker brings up (see above), KNOWLEDGE_NONE when it has
 * none of at least KNOWLEDGE_DISCOVERED_LEVEL. */
KnowledgeType conversation_pick_topic(const Colonist *speaker, uint64_t tick, Rng *rng);

/* Starts a chat between two colonists (ids of each other kept for the
 * player). Returns whether it is about knowledge. */
bool conversation_hold(Colonist *a, EntityId a_id, Colonist *b, EntityId b_id, uint64_t tick,
                       Rng *rng);

/* The check of every CONVERSATION_CHECK_TICKS over the living colonists
 * (`colonists` indexed by slot). Returns the number of chats started. */
int conversations_update(Colonist *colonists, const Pool *pool, uint64_t tick, Rng *rng);

#endif /* COLONY_CORE_CONVERSATION_H */
