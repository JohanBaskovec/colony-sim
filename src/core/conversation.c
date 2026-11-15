#include "core/conversation.h"

#include <stdlib.h>

bool conversation_available(const Colonist *colonist)
{
    return colonist->chat_ticks <= 0 && colonist_can_chat(colonist);
}

bool conversation_in_range(const Colonist *a, const Colonist *b)
{
    int dx = a->x - b->x, dy = a->y - b->y;
    return dx * dx + dy * dy <= CONVERSATION_RANGE * CONVERSATION_RANGE;
}

static double topic_weight(const Colonist *speaker, int knowledge, uint64_t tick)
{
    double level = speaker->knowledge[knowledge];
    if (level < KNOWLEDGE_DISCOVERED_LEVEL) {
        return 0.0;
    }
    uint64_t practice = speaker->knowledge_practice[knowledge];
    bool recent = practice > 0 && tick >= practice && tick - practice < CONVERSATION_RECENT_TICKS;
    return recent ? level * CONVERSATION_RECENT_WEIGHT : level;
}

KnowledgeType conversation_pick_topic(const Colonist *speaker, uint64_t tick, Rng *rng)
{
    double total = 0.0;
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        total += topic_weight(speaker, knowledge, tick);
    }
    if (!(total > 0.0)) {
        return KNOWLEDGE_NONE;
    }
    double roll = (double)rng_next_float(rng) * total;
    KnowledgeType last = KNOWLEDGE_NONE;
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        double weight = topic_weight(speaker, knowledge, tick);
        if (weight <= 0.0) {
            continue;
        }
        last = (KnowledgeType)knowledge;
        if (roll < weight) {
            return last;
        }
        roll -= weight;
    }
    return last; /* Rounding. */
}

/* `listener` learns what `speaker` talks about for the length of a chat. */
static void listen(Colonist *listener, const Colonist *speaker, KnowledgeType topic,
                   uint64_t tick)
{
    if (topic == KNOWLEDGE_NONE) {
        return;
    }
    double level = speaker->knowledge[topic];
    double cap = level < KNOWLEDGE_CONVERSATION_CAP ? level : KNOWLEDGE_CONVERSATION_CAP;
    colonist_learn(listener, topic, KNOWLEDGE_SOURCE_CONVERSATION,
                   knowledge_taught_rate(KNOWLEDGE_CONVERSATION_RATE, level),
                   (double)CONVERSATION_TICKS / TICKS_PER_HOUR, cap, tick);
}

bool conversation_hold(Colonist *a, EntityId a_id, Colonist *b, EntityId b_id, uint64_t tick,
                       Rng *rng)
{
    KnowledgeType a_topic = KNOWLEDGE_NONE, b_topic = KNOWLEDGE_NONE;
    bool about_knowledge = rng_chance(rng, CONVERSATION_KNOWLEDGE_CHANCE);
    if (about_knowledge) {
        a_topic = conversation_pick_topic(a, tick, rng);
        b_topic = conversation_pick_topic(b, tick, rng);
        /* Gains for the whole chat at once, each from what the other said. */
        listen(b, a, a_topic, tick);
        listen(a, b, b_topic, tick);
    }
    mood_remember(&a->mood, THOUGHT_CHAT);
    mood_remember(&b->mood, THOUGHT_CHAT);
    a->chat_ticks = CONVERSATION_TICKS;
    a->chat_partner = b_id;
    a->chat_topic = (uint8_t)a_topic;
    b->chat_ticks = CONVERSATION_TICKS;
    b->chat_partner = a_id;
    b->chat_topic = (uint8_t)b_topic;
    return about_knowledge;
}

int conversations_update(Colonist *colonists, const Pool *pool, uint64_t tick, Rng *rng)
{
    const float chance =
        CONVERSATION_CHATS_PER_HOUR * (float)CONVERSATION_CHECK_TICKS / (float)TICKS_PER_HOUR;
    int started = 0;
    /* Fixed slot order: deterministic. */
    for (uint32_t i = 0; i < pool->high_water; i++) {
        EntityId a_id = pool_id_at(pool, i);
        if (entity_id_is_none(a_id) || !conversation_available(&colonists[i])) {
            continue;
        }
        for (uint32_t j = i + 1; j < pool->high_water; j++) {
            EntityId b_id = pool_id_at(pool, j);
            if (entity_id_is_none(b_id) || !conversation_available(&colonists[j]) ||
                !conversation_in_range(&colonists[i], &colonists[j]) || !rng_chance(rng, chance)) {
                continue;
            }
            conversation_hold(&colonists[i], a_id, &colonists[j], b_id, tick, rng);
            started++;
            break; /* One chat at a time. */
        }
    }
    return started;
}
