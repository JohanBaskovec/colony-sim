#include "core/conversation.h"

#include "core/animal.h"
#include "core/i18n.h"
#include "core/util.h"
#include "core/weather.h"

#include <math.h>
#include <stdlib.h>

/* ---- Phrases --------------------------------------------------------------- */

static const char *const FOOD_KEYS[ITEM_TYPE_COUNT] = {
    [ITEM_BLACKBERRIES] = N_("topic.food.blackberries"),
    [ITEM_HAZELNUTS] = N_("topic.food.hazelnuts"),
    [ITEM_MUSHROOMS] = N_("topic.food.mushrooms"),
    [ITEM_WILD_GARLIC] = N_("topic.food.wild_garlic"),
    [ITEM_NETTLES] = N_("topic.food.nettles"),
    [ITEM_PLANTAIN] = N_("topic.food.plantain"),
    [ITEM_WILD_CARROTS] = N_("topic.food.wild_carrots"),
    [ITEM_CATTAIL_RHIZOMES] = N_("topic.food.cattail_rhizomes"),
    [ITEM_ROASTED_RHIZOMES] = N_("topic.food.roasted_rhizomes"),
    [ITEM_ROASTED_CARROTS] = N_("topic.food.roasted_carrots"),
    [ITEM_WOOD_SORREL] = N_("topic.food.wood_sorrel"),
    [ITEM_WILD_STRAWBERRIES] = N_("topic.food.wild_strawberries"),
    [ITEM_BILBERRIES] = N_("topic.food.bilberries"),
    [ITEM_ROSE_HIPS] = N_("topic.food.rose_hips"),
    [ITEM_CHESTNUTS] = N_("topic.food.chestnuts"),
    [ITEM_APPLES] = N_("topic.food.apples"),
    [ITEM_PEARS] = N_("topic.food.pears"),
    [ITEM_RAW_MEAT] = N_("topic.food.raw_meat"),
    [ITEM_ROASTED_MEAT] = N_("topic.food.roasted_meat"),
    [ITEM_FAT] = N_("topic.food.fat"),
    [ITEM_MARROW] = N_("topic.food.marrow"),
};

static const char *const WEATHER_KEYS[JOURNAL_WEATHER_SNOW + 1] = {
    [WEATHER_CLEAR] = N_("topic.weather.clear"), [WEATHER_CLOUDY] = N_("topic.weather.cloudy"),
    [WEATHER_FOG] = N_("topic.weather.fog"),     [WEATHER_RAIN] = N_("topic.weather.rain"),
    [WEATHER_STORM] = N_("topic.weather.storm"), [JOURNAL_WEATHER_SNOW] = N_("topic.weather.snow"),
};
_Static_assert(WEATHER_TYPE_COUNT == JOURNAL_WEATHER_SNOW, "snow comes after the skies");

static const char *const COMPLAINT_KEYS[THOUGHT_COUNT] = {
    [THOUGHT_HUNGRY] = N_("topic.complaint.hungry"),
    [THOUGHT_STARVING] = N_("topic.complaint.starving"),
    [THOUGHT_THIRSTY] = N_("topic.complaint.thirsty"),
    [THOUGHT_PARCHED] = N_("topic.complaint.parched"),
    [THOUGHT_TIRED] = N_("topic.complaint.tired"),
    [THOUGHT_EXHAUSTED] = N_("topic.complaint.exhausted"),
    [THOUGHT_COLD] = N_("topic.complaint.cold"),
    [THOUGHT_HOT] = N_("topic.complaint.hot"),
    [THOUGHT_SOAKED] = N_("topic.complaint.soaked"),
    [THOUGHT_IN_THE_RAIN] = N_("topic.complaint.in_the_rain"),
    [THOUGHT_LONELY] = N_("topic.complaint.lonely"),
};

static const char *const KNOWLEDGE_TOPIC_KEYS[KNOWLEDGE_COUNT] = {
    [KNOWLEDGE_FIRE] = N_("topic.knowledge.fire"),
    [KNOWLEDGE_STONE_KNAPPING] = N_("topic.knowledge.stone_knapping"),
    [KNOWLEDGE_GATHERING] = N_("topic.knowledge.gathering"),
    [KNOWLEDGE_SPEAR_MAKING] = N_("topic.knowledge.spear_making"),
    [KNOWLEDGE_SPEAR_HUNTING] = N_("topic.knowledge.spear_hunting"),
    [KNOWLEDGE_BUTCHERING] = N_("topic.knowledge.butchering"),
    [KNOWLEDGE_BASIC_SHELTERS] = N_("topic.knowledge.basic_shelters"),
    [KNOWLEDGE_CORDAGE] = N_("topic.knowledge.cordage"),
    [KNOWLEDGE_BASKETRY] = N_("topic.knowledge.basketry"),
    [KNOWLEDGE_SKIN_WORKING] = N_("topic.knowledge.skin_working"),
    [KNOWLEDGE_BONE_WORKING] = N_("topic.knowledge.bone_working"),
    [KNOWLEDGE_HAFTING] = N_("topic.knowledge.hafting"),
    [KNOWLEDGE_FELLING] = N_("topic.knowledge.felling"),
};

static const char *const KNOWHOW_KEYS[KNOWLEDGE_COUNT] = {
    [KNOWLEDGE_FIRE] = N_("knowhow.fire"),
    [KNOWLEDGE_STONE_KNAPPING] = N_("knowhow.stone_knapping"),
    [KNOWLEDGE_GATHERING] = N_("knowhow.gathering"),
    [KNOWLEDGE_SPEAR_MAKING] = N_("knowhow.spear_making"),
    [KNOWLEDGE_SPEAR_HUNTING] = N_("knowhow.spear_hunting"),
    [KNOWLEDGE_BUTCHERING] = N_("knowhow.butchering"),
    [KNOWLEDGE_BASIC_SHELTERS] = N_("knowhow.basic_shelters"),
    [KNOWLEDGE_CORDAGE] = N_("knowhow.cordage"),
    [KNOWLEDGE_BASKETRY] = N_("knowhow.basketry"),
    [KNOWLEDGE_SKIN_WORKING] = N_("knowhow.skin_working"),
    [KNOWLEDGE_BONE_WORKING] = N_("knowhow.bone_working"),
    [KNOWLEDGE_HAFTING] = N_("knowhow.hafting"),
    [KNOWLEDGE_FELLING] = N_("knowhow.felling"),
};

static const char *const STORY_KEYS[EXPERIENCE_TYPE_COUNT] = {
    [EXPERIENCE_KILLED_ANIMAL] = N_("story.killed_animal"),
    [EXPERIENCE_ATTACKED_BY_ANIMAL] = N_("story.attacked_by_animal"),
    [EXPERIENCE_SAW_DEATH] = N_("story.saw_death"),
    [EXPERIENCE_LOST_CLOSE_ONE] = N_("story.lost_close_one"),
    [EXPERIENCE_STARVED] = N_("story.starved"),
    [EXPERIENCE_PARCHED] = N_("story.parched"),
    [EXPERIENCE_COLD_NIGHT] = N_("story.cold_night"),
};

static const char *const LIE_KEYS[LIE_TYPE_COUNT] = {
    [LIE_CAVE_BEAR] = N_("story.lie.cave_bear"),
    [LIE_WOLF_BARE_HANDS] = N_("story.lie.wolf_bare_hands"),
    [LIE_AUROCHS] = N_("story.lie.aurochs"),
    [LIE_OUTRAN_STAG] = N_("story.lie.outran_stag"),
    [LIE_THREE_BOARS] = N_("story.lie.three_boars"),
    [LIE_MAMMOTH] = N_("story.lie.mammoth"),
    [LIE_FLINT_CAVE] = N_("story.lie.flint_cave"),
    [LIE_WINTER_ALONE] = N_("story.lie.winter_alone"),
};

static const char *const ANIMAL_KEYS[ANIMAL_SPECIES_COUNT][ANIMAL_SEX_COUNT] = {
    [ANIMAL_ROE_DEER] = {N_("story.animal.roe_deer.female"), N_("story.animal.roe_deer.male")},
    [ANIMAL_RED_DEER] = {N_("story.animal.red_deer.female"), N_("story.animal.red_deer.male")},
    [ANIMAL_BOAR] = {N_("story.animal.boar.female"), N_("story.animal.boar.male")},
    [ANIMAL_HARE] = {N_("story.animal.hare.female"), N_("story.animal.hare.male")},
    [ANIMAL_WOLF] = {N_("story.animal.wolf.female"), N_("story.animal.wolf.male")},
    [ANIMAL_FOX] = {N_("story.animal.fox.female"), N_("story.animal.fox.male")},
};

const char *conversation_topic_key(ChatTopic topic, int what)
{
    switch (topic) {
    case CHAT_TOPIC_KNOWLEDGE:
        return what >= 0 && what < KNOWLEDGE_COUNT ? KNOWLEDGE_TOPIC_KEYS[what] : NULL;
    case CHAT_TOPIC_FOOD: return what >= 0 && what < ITEM_TYPE_COUNT ? FOOD_KEYS[what] : NULL;
    case CHAT_TOPIC_WEATHER:
        return what >= 0 && what <= JOURNAL_WEATHER_SNOW ? WEATHER_KEYS[what] : NULL;
    case CHAT_TOPIC_COMPLAINT:
        return what >= 0 && what < THOUGHT_COUNT ? COMPLAINT_KEYS[what] : NULL;
    default: return NULL;
    }
}

const char *conversation_knowhow_key(KnowledgeType knowledge)
{
    return (unsigned)knowledge < KNOWLEDGE_COUNT ? KNOWHOW_KEYS[knowledge] : NULL;
}

const char *conversation_story_key(StoryKind kind, int what)
{
    if (kind == STORY_LIE) {
        return what >= 0 && what < LIE_TYPE_COUNT ? LIE_KEYS[what] : NULL;
    }
    return what >= 0 && what < EXPERIENCE_TYPE_COUNT ? STORY_KEYS[what] : NULL;
}

const char *conversation_animal_key(int species, int sex)
{
    if (species < 0 || species >= ANIMAL_SPECIES_COUNT) {
        return NULL;
    }
    return ANIMAL_KEYS[species][sex == ANIMAL_MALE ? ANIMAL_MALE : ANIMAL_FEMALE];
}

/* ---- Who chats -------------------------------------------------------------- */

bool conversation_available(const Colonist *colonist)
{
    return colonist->chat_ticks <= 0 && colonist_can_chat(colonist);
}

bool conversation_in_range(const Colonist *a, const Colonist *b)
{
    int dx = a->x - b->x, dy = a->y - b->y;
    return dx * dx + dy * dy <= CONVERSATION_RANGE * CONVERSATION_RANGE;
}

static float trait(const Colonist *colonist, AttributeType type)
{
    return attribute_position(&colonist->attributes, type);
}

static float opinion(const ConversationWorld *world, int who, int whom)
{
    return social_opinion(world->social, world->colonists, who, whom, world->tick);
}

float conversation_chance(const ConversationWorld *world, int a, int b)
{
    const Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    float base =
        CONVERSATION_CHATS_PER_HOUR * (float)CONVERSATION_CHECK_TICKS / (float)TICKS_PER_HOUR;
    float sociable = sqrtf(attribute_factor(&ca->attributes, ATTRIBUTE_SOCIABILITY) *
                           attribute_factor(&cb->attributes, ATTRIBUTE_SOCIABILITY));
    float bold =
        1.0f + 0.3f * 0.5f * (trait(ca, ATTRIBUTE_CONFIDENCE) + trait(cb, ATTRIBUTE_CONFIDENCE));
    float liking =
        clampf(1.0f + 0.5f * (opinion(world, a, b) + opinion(world, b, a)) / 150.0f, 0.4f, 1.6f);
    float lonely =
        ca->company < COLONIST_LONELY_BELOW || cb->company < COLONIST_LONELY_BELOW ? 1.5f : 1.0f;
    return clampf(base * sociable * bold * liking * lonely, 0.0f, 1.0f);
}

/* ---- Knowledge ------------------------------------------------------------- */

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
static void listen(Colonist *listener, const Colonist *speaker, KnowledgeType topic, uint64_t tick)
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

/* ---- Choices ---------------------------------------------------------------- */

/* Picks an index by weight (all >= 0); -1 when they add up to nothing. */
static int pick_weighted(const float *weights, int count, Rng *rng)
{
    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        total += weights[i];
    }
    if (!(total > 0.0f)) {
        return -1;
    }
    float roll = rng_next_float(rng) * total;
    int last = -1;
    for (int i = 0; i < count; i++) {
        if (weights[i] <= 0.0f) {
            continue;
        }
        last = i;
        if (roll < weights[i]) {
            return i;
        }
        roll -= weights[i];
    }
    return last;
}

/* The settler `a` thinks most of, or least (beyond CONVERSATION_GOSSIP_OPINION
 * either way), other than `b`; -1 when none. */
int conversation_gossip_subject(const ConversationWorld *world, int a, int b)
{
    int best = -1;
    float strongest = CONVERSATION_GOSSIP_OPINION;
    for (uint32_t slot = 0; slot < world->pool->high_water; slot++) {
        int c = (int)slot;
        if (c == a || c == b || entity_id_is_none(pool_id_at(world->pool, slot)) ||
            health_is_dead(&world->colonists[c].health)) {
            continue;
        }
        float strength = fabsf(opinion(world, a, c));
        if (strength >= strongest) {
            strongest = strength;
            best = c;
        }
    }
    return best;
}

static ChatKind pick_kind(const ConversationWorld *world, int a, int b)
{
    Rng *rng = &world->social->rng;
    if (rng_chance(rng, CONVERSATION_KNOWLEDGE_CHANCE)) {
        return CHAT_KNOWLEDGE;
    }
    const Colonist *ca = &world->colonists[a];
    float kindness = trait(ca, ATTRIBUTE_KINDNESS);
    float confidence = trait(ca, ATTRIBUTE_CONFIDENCE);
    float liking = opinion(world, a, b);
    float weights[CHAT_KIND_COUNT] = {0};
    weights[CHAT_SMALL_TALK] = 0.5f;
    weights[CHAT_STORY] = ca->experiences.count > 0 ? 0.25f : 0.0f;
    weights[CHAT_LIE] = 0.02f * (1.0f + 3.0f * fmaxf(0.0f, -kindness)) * (1.0f + confidence);
    weights[CHAT_COMPLIMENT] =
        (0.01f + 0.3f * fmaxf(0.0f, kindness)) * (liking >= 0.0f ? 1.0f : 0.3f);
    weights[CHAT_INSULT] = (0.01f + 0.3f * fmaxf(0.0f, -kindness)) * (liking < 0.0f ? 2.0f : 0.5f);
    weights[CHAT_GOSSIP] = conversation_gossip_subject(world, a, b) >= 0 ? 0.12f : 0.0f;
    const SocialTale *tale = social_tale_about(world->social, a, b, world->tick);
    if (tale != NULL) {
        /* Ill words are passed on by those who like the listener better than
         * the teller. */
        weights[CHAT_REPORT] = tale->well                                 ? 0.25f
                               : liking > opinion(world, a, tale->teller) ? 0.35f
                                                                          : 0.06f;
    }
    if (liking < 0.0f) {
        weights[CHAT_ARGUMENT] =
            (0.01f + 0.15f * fmaxf(0.0f, -kindness)) * fminf(-liking / 25.0f, 3.0f);
    }
    if (kindness > -0.25f && social_owes_apology(world->social, b, a, world->tick)) {
        weights[CHAT_APOLOGY] = 0.5f * (1.0f + kindness);
    }
    int kind = pick_weighted(weights, CHAT_KIND_COUNT, rng);
    return kind >= 0 ? (ChatKind)kind : CHAT_SMALL_TALK;
}

static ChatQuality roll_quality(const ConversationWorld *world, int a, int b)
{
    const Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    float ease = 0.5f * (trait(ca, ATTRIBUTE_SOCIAL_EASE) + trait(cb, ATTRIBUTE_SOCIAL_EASE));
    float liking = 0.5f * (opinion(world, a, b) + opinion(world, b, a));
    float awkward = clampf(0.12f - 0.12f * ease - liking / 400.0f, 0.02f, 0.4f);
    float pleasant = clampf(0.45f + 0.25f * ease + liking / 250.0f, 0.1f, 0.9f);
    float roll = rng_next_float(&world->social->rng);
    if (roll < awkward) {
        return CHAT_AWKWARD;
    }
    return roll < awkward + pleasant ? CHAT_PLEASANT : CHAT_NEUTRAL;
}

/* What `speaker` brings up in small talk: a recent meal, the weather, or
 * what it suffers from most. */
static ChatTopic small_talk_topic(const ConversationWorld *world, const Colonist *speaker,
                                  int *what)
{
    const Mood *mood = &speaker->mood;
    int meal = mood->meal_count > 0 ? mood->meals[(mood->meal_next + MOOD_MEALS - 1) % MOOD_MEALS]
                                    : ITEM_NONE;
    int worst = -1;
    float worst_value = 0.0f;
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        float value = mood_thought_value(mood, (ThoughtType)type);
        if (COMPLAINT_KEYS[type] != NULL && (mood->states & THOUGHT_BIT(type)) != 0 &&
            value < worst_value) {
            worst_value = value;
            worst = type;
        }
    }
    float weights[CHAT_TOPIC_COUNT] = {0};
    weights[CHAT_TOPIC_NONE] = 0.4f;
    weights[CHAT_TOPIC_FOOD] = conversation_topic_key(CHAT_TOPIC_FOOD, meal) != NULL ? 1.0f : 0.0f;
    weights[CHAT_TOPIC_WEATHER] =
        conversation_topic_key(CHAT_TOPIC_WEATHER, world->weather) != NULL ? 0.6f : 0.0f;
    weights[CHAT_TOPIC_COMPLAINT] = worst >= 0 ? 1.2f : 0.0f;
    int topic = pick_weighted(weights, CHAT_TOPIC_COUNT, &world->social->rng);
    switch (topic) {
    case CHAT_TOPIC_FOOD: *what = meal; return CHAT_TOPIC_FOOD;
    case CHAT_TOPIC_WEATHER: *what = world->weather; return CHAT_TOPIC_WEATHER;
    case CHAT_TOPIC_COMPLAINT: *what = worst; return CHAT_TOPIC_COMPLAINT;
    default: *what = 0; return CHAT_TOPIC_NONE;
    }
}

/* ---- Chats ------------------------------------------------------------------ */

/* Both remember how the chat went: a memory of each other, the company, and
 * a good thought unless it was awkward. */
static void chat_went(const ConversationWorld *world, int a, int b, ChatQuality quality)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    float ease_a = trait(ca, ATTRIBUTE_SOCIAL_EASE), ease_b = trait(cb, ATTRIBUTE_SOCIAL_EASE);
    float company = CONVERSATION_COMPANY_NEUTRAL;
    switch (quality) {
    case CHAT_PLEASANT:
        social_remember(world->social, b, a, SOCIAL_MEMORY_PLEASANT_CHAT, 1.0f + 0.5f * ease_a,
                        world->tick);
        social_remember(world->social, a, b, SOCIAL_MEMORY_PLEASANT_CHAT, 1.0f + 0.5f * ease_b,
                        world->tick);
        company = CONVERSATION_COMPANY_PLEASANT;
        break;
    case CHAT_AWKWARD:
        social_remember(world->social, b, a, SOCIAL_MEMORY_AWKWARD_CHAT, 1.0f - 0.5f * ease_a,
                        world->tick);
        social_remember(world->social, a, b, SOCIAL_MEMORY_AWKWARD_CHAT, 1.0f - 0.5f * ease_b,
                        world->tick);
        company = CONVERSATION_COMPANY_AWKWARD;
        break;
    default:
        social_remember(world->social, b, a, SOCIAL_MEMORY_PLEASANT_CHAT, 0.4f + 0.2f * ease_a,
                        world->tick);
        social_remember(world->social, a, b, SOCIAL_MEMORY_PLEASANT_CHAT, 0.4f + 0.2f * ease_b,
                        world->tick);
        break;
    }
    ca->company = clampf(ca->company + company, 0.0f, 1.0f);
    cb->company = clampf(cb->company + company, 0.0f, 1.0f);
    if (quality != CHAT_AWKWARD) {
        mood_remember(&ca->mood, THOUGHT_CHAT);
        mood_remember(&cb->mood, THOUGHT_CHAT);
    }
}

static void note_chat(const ConversationWorld *world, int a, int b, ChatTopic topic, int what,
                      ChatQuality quality)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    journal_note_chat(&ca->journal, world->tick, cb->name, topic, what, false, quality);
    journal_note_chat(&cb->journal, world->tick, ca->name, topic, what, true, quality);
}

static ChatResult small_talk(const ConversationWorld *world, int a, int b, ChatQuality quality)
{
    int what = 0;
    ChatTopic topic = small_talk_topic(world, &world->colonists[a], &what);
    note_chat(world, a, b, topic, what, quality);
    chat_went(world, a, b, quality);
    return (ChatResult){.kind = CHAT_SMALL_TALK, .quality = quality};
}

static ChatResult knowledge_chat(const ConversationWorld *world, int a, int b, ChatQuality quality)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    Rng *rng = &world->social->rng;
    KnowledgeType a_topic = conversation_pick_topic(ca, world->tick, rng);
    KnowledgeType b_topic = conversation_pick_topic(cb, world->tick, rng);
    /* Gains for the whole chat at once, each from what the other said. */
    listen(cb, ca, a_topic, world->tick);
    listen(ca, cb, b_topic, world->tick);
    ca->chat_topic = (uint8_t)a_topic;
    cb->chat_topic = (uint8_t)b_topic;
    if (a_topic != KNOWLEDGE_NONE &&
        ca->knowledge[a_topic] >= cb->knowledge[a_topic] + CONVERSATION_EXPLAIN_GAP) {
        JournalSocial details = {.what = (uint16_t)a_topic};
        journal_note_social(&ca->journal, world->tick, JOURNAL_EXPLAINED, cb->name, details);
        journal_note_social(&cb->journal, world->tick, JOURNAL_LEARNED, ca->name, details);
        social_remember(world->social, b, a, SOCIAL_MEMORY_TAUGHT_ME, 1.0f, world->tick);
        chat_went(world, a, b, quality);
        return (ChatResult){.kind = CHAT_EXPLAIN, .quality = quality};
    }
    /* Both remember what the chat was about: what one of them brought up. */
    KnowledgeType topic = a_topic != KNOWLEDGE_NONE ? a_topic : b_topic;
    if (topic == KNOWLEDGE_NONE) {
        note_chat(world, a, b, CHAT_TOPIC_NONE, 0, quality);
    } else {
        note_chat(world, a, b, CHAT_TOPIC_KNOWLEDGE, topic, quality);
    }
    chat_went(world, a, b, quality);
    return (ChatResult){.kind = CHAT_KNOWLEDGE, .quality = quality};
}

/* What a story is worth to the listener, by how the chat went. */
static float story_scale(ChatQuality quality)
{
    return quality == CHAT_PLEASANT ? 1.2f : quality == CHAT_AWKWARD ? 0.3f : 0.8f;
}

static ChatResult story(const ConversationWorld *world, int a, int b, ChatQuality quality)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    const Experience *experience = experiences_pick(
        &ca->experiences, (uint32_t)(world->tick / TICKS_PER_HOUR), &world->social->rng);
    if (experience == NULL) {
        return small_talk(world, a, b, quality);
    }
    bool animal = experience->type == EXPERIENCE_KILLED_ANIMAL ||
                  experience->type == EXPERIENCE_ATTACKED_BY_ANIMAL;
    JournalSocial details = {.topic = STORY_TRUE,
                             .what = experience->type,
                             .animal = animal ? (uint8_t)(experience->species + 1) : 0,
                             .animal_sex = experience->sex,
                             .subject = experience->name[0] != '\0' ? experience->name : NULL};
    journal_note_social(&ca->journal, world->tick, JOURNAL_TOLD_STORY, cb->name, details);
    journal_note_social(&cb->journal, world->tick, JOURNAL_HEARD_STORY, ca->name, details);
    /* A kill or a fight is a tale; the rest, something hard confided. */
    social_remember(world->social, b, a, animal ? SOCIAL_MEMORY_GOOD_STORY : SOCIAL_MEMORY_CONFIDED,
                    story_scale(quality), world->tick);
    chat_went(world, a, b, quality);
    return (ChatResult){.kind = CHAT_STORY, .quality = quality};
}

static ChatResult lie(const ConversationWorld *world, int a, int b, ChatQuality quality)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    Rng *rng = &world->social->rng;
    int type = rng_range_i32(rng, 0, LIE_TYPE_COUNT - 1);
    float caught = clampf(0.45f - 0.3f * trait(ca, ATTRIBUTE_SOCIAL_EASE) +
                              0.15f * trait(cb, ATTRIBUTE_CONFIDENCE),
                          0.1f, 0.85f);
    bool found_out = rng_chance(rng, caught);
    JournalSocial details = {.topic = STORY_LIE, .what = (uint16_t)type, .outcome = found_out};
    journal_note_social(&ca->journal, world->tick, JOURNAL_LIED, cb->name, details);
    if (found_out) {
        journal_note_social(&cb->journal, world->tick, JOURNAL_CAUGHT_LIE, ca->name, details);
        social_remember(world->social, b, a, SOCIAL_MEMORY_LIED_TO_ME, 1.0f, world->tick);
        quality = CHAT_AWKWARD;
    } else {
        journal_note_social(&cb->journal, world->tick, JOURNAL_HEARD_STORY, ca->name, details);
        social_remember(world->social, b, a, SOCIAL_MEMORY_GOOD_STORY, story_scale(quality),
                        world->tick);
    }
    chat_went(world, a, b, quality);
    return (ChatResult){.kind = CHAT_LIE, .quality = quality, .found_out = found_out};
}

static KnowledgeType best_knowledge(const Colonist *colonist)
{
    KnowledgeType best = KNOWLEDGE_NONE;
    double level = 30.0; /* Something worth praising. */
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        if (colonist->knowledge[knowledge] >= level) {
            level = colonist->knowledge[knowledge];
            best = (KnowledgeType)knowledge;
        }
    }
    return best;
}

static ChatResult compliment(const ConversationWorld *world, int a, int b)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    KnowledgeType skill = best_knowledge(cb);
    float weights[COMPLIMENT_TYPE_COUNT] = {
        [COMPLIMENT_GENERIC] = 1.0f,
        [COMPLIMENT_LOOKS] = trait(cb, ATTRIBUTE_BEAUTY) >= 0.25f ? 1.5f : 0.0f,
        [COMPLIMENT_SKILL] = skill != KNOWLEDGE_NONE ? 1.5f : 0.0f,
    };
    int type = pick_weighted(weights, COMPLIMENT_TYPE_COUNT, &world->social->rng);
    JournalSocial details = {.what = (uint16_t)type,
                             .topic = type == COMPLIMENT_SKILL ? (uint8_t)skill : 0};
    journal_note_social(&ca->journal, world->tick, JOURNAL_COMPLIMENT, cb->name, details);
    journal_note_social(&cb->journal, world->tick, JOURNAL_COMPLIMENTED, ca->name, details);
    social_remember(world->social, b, a, SOCIAL_MEMORY_COMPLIMENTED_ME,
                    1.0f + 0.3f * trait(ca, ATTRIBUTE_SOCIAL_EASE), world->tick);
    mood_remember(&cb->mood, THOUGHT_COMPLIMENTED);
    chat_went(world, a, b, CHAT_PLEASANT);
    return (ChatResult){.kind = CHAT_COMPLIMENT, .quality = CHAT_PLEASANT};
}

/* Bruises from a brawl, on a part drawn by coverage: never as far as half
 * of what the part can take (a brawl neither maims nor kills). */
static void bruise(Colonist *colonist, Rng *rng)
{
    Health *health = &colonist->health;
    float damage =
        CONVERSATION_BRAWL_DAMAGE_MIN +
        (CONVERSATION_BRAWL_DAMAGE_MAX - CONVERSATION_BRAWL_DAMAGE_MIN) * rng_next_float(rng);
    int part = health_random_part(health, rng);
    if (part < 0 || health_part_damage(health, part) + damage * health->damage_factor >=
                        0.5f * health_part_hit_points(health, part)) {
        return;
    }
    health->hurt_cause = HURT_BRAWL;
    health_hurt(health, part, INJURY_BRUISE, damage);
    health->hurt_cause = HURT_OTHER;
}

/* `a` and `b` argue: bad memories and moods on both sides, and maybe blows
 * (more between unkind, bold settlers who dislike each other). Returns
 * whether it came to blows. */
static bool argue(const ConversationWorld *world, int a, int b, bool blows_for_sure)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    Rng *rng = &world->social->rng;
    journal_note_social(&ca->journal, world->tick, JOURNAL_ARGUED, cb->name, (JournalSocial){0});
    journal_note_social(&cb->journal, world->tick, JOURNAL_ARGUED, ca->name, (JournalSocial){0});
    social_remember(world->social, a, b, SOCIAL_MEMORY_ARGUED, 1.0f, world->tick);
    social_remember(world->social, b, a, SOCIAL_MEMORY_ARGUED, 1.0f, world->tick);
    mood_remember(&ca->mood, THOUGHT_ARGUMENT);
    mood_remember(&cb->mood, THOUGHT_ARGUMENT);
    float kindness = 0.5f * (trait(ca, ATTRIBUTE_KINDNESS) + trait(cb, ATTRIBUTE_KINDNESS));
    float confidence = 0.5f * (trait(ca, ATTRIBUTE_CONFIDENCE) + trait(cb, ATTRIBUTE_CONFIDENCE));
    float liking = 0.5f * (opinion(world, a, b) + opinion(world, b, a));
    float blows =
        clampf(0.1f - 0.15f * kindness + 0.1f * confidence - liking / 300.0f, 0.02f, 0.5f);
    if (health_is_down(&ca->health) || health_is_down(&cb->health) ||
        (!blows_for_sure && !rng_chance(rng, blows))) {
        return false;
    }
    journal_note_social(&ca->journal, world->tick, JOURNAL_BRAWL, cb->name, (JournalSocial){0});
    journal_note_social(&cb->journal, world->tick, JOURNAL_BRAWL, ca->name, (JournalSocial){0});
    bruise(ca, rng);
    bruise(cb, rng);
    social_remember(world->social, a, b, SOCIAL_MEMORY_FOUGHT, 1.0f, world->tick);
    social_remember(world->social, b, a, SOCIAL_MEMORY_FOUGHT, 1.0f, world->tick);
    mood_remember(&ca->mood, THOUGHT_FOUGHT);
    mood_remember(&cb->mood, THOUGHT_FOUGHT);
    return true;
}

static ChatResult argument(const ConversationWorld *world, int a, int b)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    ca->company = clampf(ca->company + CONVERSATION_COMPANY_INSULTED, 0.0f, 1.0f);
    cb->company = clampf(cb->company + CONVERSATION_COMPANY_INSULTED, 0.0f, 1.0f);
    ChatResult result = {.kind = CHAT_ARGUMENT, .quality = CHAT_AWKWARD};
    result.argued = true;
    result.brawl = argue(world, a, b, false);
    return result;
}

static ChatResult insult(const ConversationWorld *world, int a, int b)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    float weights[INSULT_TYPE_COUNT] = {
        [INSULT_GENERIC] = 1.0f,
        [INSULT_LOOKS] = trait(cb, ATTRIBUTE_BEAUTY) <= -0.25f ? 1.5f : 0.0f,
        [INSULT_LAZY] = trait(cb, ATTRIBUTE_WORK_PACE) <= -0.25f ? 1.5f : 0.0f,
        [INSULT_SMELL] = cb->wetness >= MOOD_SOAKED_WETNESS ? 1.5f : 0.0f,
    };
    int type = pick_weighted(weights, INSULT_TYPE_COUNT, &world->social->rng);
    JournalSocial details = {.what = (uint16_t)type};
    journal_note_social(&ca->journal, world->tick, JOURNAL_INSULT, cb->name, details);
    journal_note_social(&cb->journal, world->tick, JOURNAL_INSULTED, ca->name, details);
    /* Self-assured settlers shrug insults off. */
    social_remember(world->social, b, a, SOCIAL_MEMORY_INSULTED_ME,
                    clampf(1.0f - 0.5f * trait(cb, ATTRIBUTE_CONFIDENCE), 0.4f, 1.5f), world->tick);
    mood_remember(&cb->mood, THOUGHT_INSULTED);
    ca->company = clampf(ca->company + CONVERSATION_COMPANY_AWKWARD, 0.0f, 1.0f);
    cb->company = clampf(cb->company + CONVERSATION_COMPANY_INSULTED, 0.0f, 1.0f);
    ChatResult result = {.kind = CHAT_INSULT, .quality = CHAT_AWKWARD};
    /* The bold and the unkind answer back, more so to those they dislike. */
    float retort = clampf(0.2f + 0.25f * trait(cb, ATTRIBUTE_CONFIDENCE) -
                              0.25f * trait(cb, ATTRIBUTE_KINDNESS) - opinion(world, b, a) / 200.0f,
                          0.05f, 0.75f);
    if (rng_chance(&world->social->rng, retort)) {
        result.argued = true;
        result.brawl = argue(world, b, a, false);
    }
    return result;
}

static ChatResult gossip(const ConversationWorld *world, int a, int b, ChatQuality quality)
{
    int c = conversation_gossip_subject(world, a, b);
    if (c < 0) {
        return small_talk(world, a, b, quality);
    }
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    bool well = opinion(world, a, c) > 0.0f;
    JournalSocial details = {.outcome = well, .subject = world->colonists[c].name};
    journal_note_social(&ca->journal, world->tick, JOURNAL_GOSSIP, cb->name, details);
    journal_note_social(&cb->journal, world->tick, JOURNAL_HEARD_GOSSIP, ca->name, details);
    /* Believed as much as the teller is liked. */
    float trust = clampf(0.5f + opinion(world, b, a) / 100.0f, 0.2f, 1.5f);
    social_remember(world->social, b, c, well ? SOCIAL_MEMORY_GOOD_WORD : SOCIAL_MEMORY_BAD_WORD,
                    trust, world->tick);
    social_hear_tale(world->social, b, a, c, well, world->tick); /* To pass on, maybe. */
    chat_went(world, a, b, quality);
    return (ChatResult){.kind = CHAT_GOSSIP, .quality = quality};
}

/* `a` tells `b` what a third settler said of `b` (a tale `a` heard). */
static ChatResult report(const ConversationWorld *world, int a, int b, ChatQuality quality)
{
    SocialTale tale;
    if (!social_take_tale(world->social, a, b, world->tick, &tale) ||
        entity_id_is_none(pool_id_at(world->pool, (uint32_t)tale.teller)) ||
        health_is_dead(&world->colonists[tale.teller].health)) {
        return small_talk(world, a, b, quality);
    }
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    int c = tale.teller;
    JournalSocial details = {.outcome = tale.well, .subject = world->colonists[c].name};
    journal_note_social(&ca->journal, world->tick, JOURNAL_REPORTED, cb->name, details);
    journal_note_social(&cb->journal, world->tick, JOURNAL_HEARD_REPORT, ca->name, details);
    /* Believed as much as the one who passes it on is liked. */
    float trust = clampf(0.5f + opinion(world, b, a) / 100.0f, 0.3f, 1.5f);
    social_remember(world->social, b, c,
                    tale.well ? SOCIAL_MEMORY_SPOKE_WELL_OF_ME : SOCIAL_MEMORY_SPOKE_ILL_OF_ME,
                    trust, world->tick);
    mood_remember(&cb->mood, tale.well ? THOUGHT_COMPLIMENTED : THOUGHT_INSULTED);
    chat_went(world, a, b, quality);
    return (ChatResult){.kind = CHAT_REPORT, .quality = quality};
}

/* `a` says sorry to `b` for what it did: forgiven more readily by the kind,
 * by those who like `a`, and for small wrongs. */
static ChatResult apology(const ConversationWorld *world, int a, int b)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    float wrongs = social_wrongs(world->social, b, a, world->tick);
    float accept =
        clampf(0.55f + 0.3f * trait(cb, ATTRIBUTE_KINDNESS) + opinion(world, b, a) / 150.0f +
                   0.15f * trait(ca, ATTRIBUTE_SOCIAL_EASE) - wrongs / 100.0f,
               0.1f, 0.95f);
    bool forgiven = rng_chance(&world->social->rng, accept);
    JournalSocial details = {.outcome = forgiven};
    journal_note_social(&ca->journal, world->tick, JOURNAL_APOLOGIZED, cb->name, details);
    journal_note_social(&cb->journal, world->tick, JOURNAL_FORGAVE, ca->name, details);
    ChatResult result = {.kind = CHAT_APOLOGY, .quality = forgiven ? CHAT_PLEASANT : CHAT_AWKWARD};
    result.forgiven = forgiven;
    if (forgiven) {
        social_forgive(world->social, b, a, SOCIAL_APOLOGY_FORGIVES);
        social_remember(world->social, b, a, SOCIAL_MEMORY_APOLOGIZED, 1.0f, world->tick);
        mood_remember(&cb->mood, THOUGHT_APOLOGIZED_TO);
    } else {
        social_remember(world->social, a, b, SOCIAL_MEMORY_REFUSED_APOLOGY, 1.0f, world->tick);
    }
    chat_went(world, a, b, result.quality);
    return result;
}

/* Both stop to chat with each other. */
static void begin_chat(const ConversationWorld *world, int a, int b)
{
    Colonist *ca = &world->colonists[a], *cb = &world->colonists[b];
    ca->chat_ticks = CONVERSATION_TICKS;
    ca->chat_partner = pool_id_at(world->pool, (uint32_t)b);
    ca->chat_topic = KNOWLEDGE_NONE;
    cb->chat_ticks = CONVERSATION_TICKS;
    cb->chat_partner = pool_id_at(world->pool, (uint32_t)a);
    cb->chat_topic = KNOWLEDGE_NONE;
}

static ChatResult counted(const ConversationWorld *world, ChatResult result)
{
    ConversationCounts *counts = world->counts;
    if (counts != NULL) {
        counts->kinds[result.kind]++;
        counts->lies_found_out += result.found_out;
        counts->answered_back += result.kind == CHAT_INSULT && result.argued;
        counts->brawls += result.brawl;
        counts->forgiven += result.kind == CHAT_APOLOGY && result.forgiven;
        counts->refused += result.kind == CHAT_APOLOGY && !result.forgiven;
    }
    return result;
}

static ChatResult hold(const ConversationWorld *world, int a, int b, ChatKind kind)
{
    begin_chat(world, a, b);
    switch (kind) {
    case CHAT_COMPLIMENT: return compliment(world, a, b);
    case CHAT_INSULT: return insult(world, a, b);
    case CHAT_ARGUMENT: return argument(world, a, b);
    case CHAT_APOLOGY: return apology(world, a, b);
    default: break;
    }
    ChatQuality quality = roll_quality(world, a, b);
    switch (kind) {
    case CHAT_KNOWLEDGE:
    case CHAT_EXPLAIN: return knowledge_chat(world, a, b, quality);
    case CHAT_STORY: return story(world, a, b, quality);
    case CHAT_LIE: return lie(world, a, b, quality);
    case CHAT_GOSSIP: return gossip(world, a, b, quality);
    case CHAT_REPORT: return report(world, a, b, quality);
    default: return small_talk(world, a, b, quality);
    }
}

ChatResult conversation_hold_as(const ConversationWorld *world, int a, int b, ChatKind kind)
{
    return counted(world, hold(world, a, b, kind));
}

ChatResult conversation_brawl(const ConversationWorld *world, int a, int b)
{
    begin_chat(world, a, b);
    ChatResult result = {.kind = CHAT_ARGUMENT, .quality = CHAT_AWKWARD, .argued = true};
    result.brawl = argue(world, a, b, true);
    return counted(world, result);
}

ChatResult conversation_hold(const ConversationWorld *world, int a, int b)
{
    return conversation_hold_as(world, a, b, pick_kind(world, a, b));
}

int conversations_update(const ConversationWorld *world)
{
    const Pool *pool = world->pool;
    int started = 0;
    /* Fixed slot order: deterministic. */
    for (uint32_t i = 0; i < pool->high_water; i++) {
        if (entity_id_is_none(pool_id_at(pool, i)) ||
            !conversation_available(&world->colonists[i])) {
            continue;
        }
        for (uint32_t j = i + 1; j < pool->high_water; j++) {
            if (entity_id_is_none(pool_id_at(pool, j)) ||
                !conversation_available(&world->colonists[j]) ||
                !conversation_in_range(&world->colonists[i], &world->colonists[j]) ||
                !rng_chance(&world->social->rng, conversation_chance(world, (int)i, (int)j))) {
                continue;
            }
            /* The bolder of the two more often leads. */
            int a = (int)i, b = (int)j;
            float lead = 0.5f + 0.25f * (trait(&world->colonists[a], ATTRIBUTE_CONFIDENCE) -
                                         trait(&world->colonists[b], ATTRIBUTE_CONFIDENCE));
            if (!rng_chance(&world->social->rng, lead)) {
                a = (int)j;
                b = (int)i;
            }
            conversation_hold(world, a, b);
            started++;
            break; /* One chat at a time. */
        }
    }
    return started;
}
