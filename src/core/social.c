#include "core/social.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HOURS_PER_YEAR ((float)TICKS_PER_YEAR / (float)TICKS_PER_HOUR)

/* id, name, value, plateau (hours), half-life (hours), stack (tuning). */
static const SocialMemoryDef MEMORY_DEFS[SOCIAL_MEMORY_TYPE_COUNT] = {
    [SOCIAL_MEMORY_JOURNEY] = {"journey", N_("social.memory.journey"), 18.0f, 24.0f * 30.0f,
                               24.0f * 60.0f, 1},
    [SOCIAL_MEMORY_PLEASANT_CHAT] = {"pleasant_chat", N_("social.memory.pleasant_chat"), 1.5f,
                                     48.0f, 120.0f, 12},
    [SOCIAL_MEMORY_AWKWARD_CHAT] = {"awkward_chat", N_("social.memory.awkward_chat"), -2.0f, 24.0f,
                                    72.0f, 5},
    [SOCIAL_MEMORY_TAUGHT_ME] = {"taught_me", N_("social.memory.taught_me"), 3.0f, 120.0f, 240.0f,
                                 5},
    [SOCIAL_MEMORY_GOOD_STORY] = {"good_story", N_("social.memory.good_story"), 2.0f, 48.0f, 120.0f,
                                  5},
    [SOCIAL_MEMORY_CONFIDED] = {"confided", N_("social.memory.confided"), 3.0f, 72.0f, 240.0f, 4},
    [SOCIAL_MEMORY_COMPLIMENTED_ME] = {"complimented_me", N_("social.memory.complimented_me"), 4.0f,
                                       72.0f, 144.0f, 4},
    [SOCIAL_MEMORY_INSULTED_ME] = {"insulted_me", N_("social.memory.insulted_me"), -7.0f, 96.0f,
                                   192.0f, 5},
    [SOCIAL_MEMORY_LIED_TO_ME] = {"lied_to_me", N_("social.memory.lied_to_me"), -10.0f, 120.0f,
                                  360.0f, 3},
    [SOCIAL_MEMORY_GOOD_WORD] = {"good_word", N_("social.memory.good_word"), 2.0f, 48.0f, 120.0f,
                                 4},
    [SOCIAL_MEMORY_BAD_WORD] = {"bad_word", N_("social.memory.bad_word"), -3.0f, 72.0f, 144.0f, 4},
    [SOCIAL_MEMORY_ATTACKED_ME] = {"attacked_me", N_("social.memory.attacked_me"), -30.0f,
                                   24.0f * 30.0f, HOURS_PER_YEAR, 3},
    /* Decades: a fifth of it is still there after 50 years. */
    [SOCIAL_MEMORY_KILLED_FAMILY] = {"killed_family", N_("social.memory.killed_family"), -80.0f,
                                     20.0f * HOURS_PER_YEAR, 15.0f * HOURS_PER_YEAR, 2},
    [SOCIAL_MEMORY_KILLED_FRIEND] = {"killed_friend", N_("social.memory.killed_friend"), -45.0f,
                                     2.0f * HOURS_PER_YEAR, 5.0f * HOURS_PER_YEAR, 2},
    [SOCIAL_MEMORY_SPOKE_WELL_OF_ME] = {"spoke_well_of_me", N_("social.memory.spoke_well_of_me"),
                                        4.0f, 72.0f, 192.0f, 3},
    [SOCIAL_MEMORY_SPOKE_ILL_OF_ME] = {"spoke_ill_of_me", N_("social.memory.spoke_ill_of_me"),
                                       -8.0f, 96.0f, 240.0f, 3},
    [SOCIAL_MEMORY_ARGUED] = {"argued", N_("social.memory.argued"), -5.0f, 48.0f, 96.0f, 4},
    [SOCIAL_MEMORY_FOUGHT] = {"fought", N_("social.memory.fought"), -12.0f, 120.0f, 360.0f, 3},
    [SOCIAL_MEMORY_APOLOGIZED] = {"apologized", N_("social.memory.apologized"), 5.0f, 72.0f, 240.0f,
                                  2},
    [SOCIAL_MEMORY_REFUSED_APOLOGY] = {"refused_apology", N_("social.memory.refused_apology"),
                                       -3.0f, 48.0f, 120.0f, 2},
    [SOCIAL_MEMORY_DEBUG] = {"debug", N_("social.memory.debug"), 1.0f, 50.0f * HOURS_PER_YEAR,
                             50.0f * HOURS_PER_YEAR, 200},
};

static const char *const FAMILY_KEYS[FAMILY_TIE_COUNT] = {
    [FAMILY_NONE] = NULL,
    [FAMILY_PARENT] = N_("social.family.parent"),
    [FAMILY_CHILD] = N_("social.family.child"),
    [FAMILY_SIBLING] = N_("social.family.sibling"),
    [FAMILY_GRANDPARENT] = N_("social.family.grandparent"),
    [FAMILY_GRANDCHILD] = N_("social.family.grandchild"),
};

static const float FAMILY_POINTS[FAMILY_TIE_COUNT] = {
    [FAMILY_PARENT] = 35.0f,      [FAMILY_CHILD] = 35.0f,      [FAMILY_SIBLING] = 30.0f,
    [FAMILY_GRANDPARENT] = 25.0f, [FAMILY_GRANDCHILD] = 25.0f,
};

static const char *const LEVEL_KEYS[RELATION_LEVEL_COUNT] = {
    [RELATION_ENEMY] = N_("social.level.enemy"),
    [RELATION_RIVAL] = N_("social.level.rival"),
    [RELATION_ACQUAINTANCE] = N_("social.level.acquaintance"),
    [RELATION_FRIEND] = N_("social.level.friend"),
    [RELATION_CLOSE_FRIEND] = N_("social.level.close_friend"),
};

const SocialMemoryDef *social_memory_def(SocialMemoryType type)
{
    return (unsigned)type < SOCIAL_MEMORY_TYPE_COUNT ? &MEMORY_DEFS[type] : NULL;
}

void social_init(Social *social, uint64_t seed)
{
    memset(social->bonds, 0, sizeof(social->bonds));
    memset(social->tales, 0, sizeof(social->tales));
    for (int who = 0; who < COLONIST_MAX; who++) {
        for (int i = 0; i < SOCIAL_TALE_MAX; i++) {
            social->tales[who][i].teller = -1;
        }
        for (int whom = 0; whom < COLONIST_MAX; whom++) {
            social->bonds[who][whom].level = RELATION_ACQUAINTANCE;
        }
    }
    rng_seed(&social->rng, seed, 0x736f6369616cULL /* "social" */);
}

void social_clear_slot(Social *social, int slot)
{
    if (slot < 0 || slot >= COLONIST_MAX) {
        return;
    }
    for (int other = 0; other < COLONIST_MAX; other++) {
        memset(&social->bonds[slot][other], 0, sizeof(SocialBond));
        memset(&social->bonds[other][slot], 0, sizeof(SocialBond));
        social->bonds[slot][other].level = RELATION_ACQUAINTANCE;
        social->bonds[other][slot].level = RELATION_ACQUAINTANCE;
        for (int i = 0; i < SOCIAL_TALE_MAX; i++) {
            SocialTale *tale = &social->tales[other][i];
            if (other == slot || tale->teller == slot || tale->subject == slot) {
                tale->teller = -1;
            }
        }
    }
}

SocialBond *social_bond(Social *social, int who, int whom)
{
    if (who < 0 || whom < 0 || who >= COLONIST_MAX || whom >= COLONIST_MAX || who == whom) {
        return NULL;
    }
    return &social->bonds[who][whom];
}

const SocialBond *social_bond_const(const Social *social, int who, int whom)
{
    return social_bond((Social *)social, who, whom);
}

FamilyTie family_tie_reverse(FamilyTie tie)
{
    switch (tie) {
    case FAMILY_PARENT: return FAMILY_CHILD;
    case FAMILY_CHILD: return FAMILY_PARENT;
    case FAMILY_GRANDPARENT: return FAMILY_GRANDCHILD;
    case FAMILY_GRANDCHILD: return FAMILY_GRANDPARENT;
    case FAMILY_SIBLING: return FAMILY_SIBLING;
    default: return FAMILY_NONE;
    }
}

bool social_set_family(Social *social, int who, int whom, FamilyTie tie)
{
    SocialBond *bond = social_bond(social, who, whom);
    SocialBond *back = social_bond(social, whom, who);
    if (bond == NULL || back == NULL || (unsigned)tie >= FAMILY_TIE_COUNT) {
        return false;
    }
    bond->family = (uint8_t)tie;
    back->family = (uint8_t)family_tie_reverse(tie);
    return true;
}

static uint32_t hour_of(uint64_t tick)
{
    return (uint32_t)(tick / TICKS_PER_HOUR);
}

static float fade(const SocialMemoryDef *def, float value, float hours)
{
    if (hours <= def->plateau_hours) {
        return value;
    }
    return value * exp2f(-(hours - def->plateau_hours) / def->half_life_hours);
}

float social_memory_weight(const SocialMemory *memory, uint64_t tick)
{
    const SocialMemoryDef *def = social_memory_def((SocialMemoryType)memory->type);
    if (def == NULL) {
        return 0.0f;
    }
    uint32_t now = hour_of(tick);
    float hours = now > memory->hour ? (float)(now - memory->hour) : 0.0f;
    return fade(def, memory->value, hours);
}

void social_remember(Social *social, int who, int whom, SocialMemoryType type, float scale,
                     uint64_t tick)
{
    SocialBond *bond = social_bond(social, who, whom);
    const SocialMemoryDef *def = social_memory_def(type);
    if (bond == NULL || def == NULL || !(scale > 0.0f)) {
        return;
    }
    float add = def->value * scale;
    /* A memory worth more than its value (a charming speaker's chats, a
     * good journey) may add up to more. */
    float limit = fabsf(def->value) * (float)def->max_stack * fmaxf(scale, 1.0f);
    for (int i = 0; i < bond->memory_count; i++) {
        SocialMemory *memory = &bond->memories[i];
        if (memory->type == type) {
            float value = clampf(social_memory_weight(memory, tick) + add, -limit, limit);
            memory->value = value;
            memory->hour = hour_of(tick);
            if (memory->count < UINT8_MAX) {
                memory->count++;
            }
            return;
        }
    }
    SocialMemory fresh = {(uint8_t)type, 1, 0, clampf(add, -limit, limit), hour_of(tick)};
    if (bond->memory_count < SOCIAL_MEMORY_MAX) {
        bond->memories[bond->memory_count++] = fresh;
        return;
    }
    int weakest = 0;
    for (int i = 1; i < bond->memory_count; i++) {
        if (fabsf(social_memory_weight(&bond->memories[i], tick)) <
            fabsf(social_memory_weight(&bond->memories[weakest], tick))) {
            weakest = i;
        }
    }
    if (fabsf(social_memory_weight(&bond->memories[weakest], tick)) < fabsf(fresh.value)) {
        bond->memories[weakest] = fresh;
    }
}

float family_tie_points(FamilyTie tie)
{
    return (unsigned)tie < FAMILY_TIE_COUNT ? FAMILY_POINTS[tie] : 0.0f;
}

static float beauty_points(const Colonist *other)
{
    return SOCIAL_BEAUTY_POINTS * attribute_position(&other->attributes, ATTRIBUTE_BEAUTY);
}

static float confidence_points(const Colonist *other)
{
    return SOCIAL_CONFIDENCE_POINTS * attribute_position(&other->attributes, ATTRIBUTE_CONFIDENCE);
}

float social_opinion(const Social *social, const Colonist *colonists, int who, int whom,
                     uint64_t tick)
{
    const SocialBond *bond = social_bond_const(social, who, whom);
    if (bond == NULL) {
        return 0.0f;
    }
    float opinion = family_tie_points((FamilyTie)bond->family) + beauty_points(&colonists[whom]) +
                    confidence_points(&colonists[whom]);
    for (int i = 0; i < bond->memory_count; i++) {
        opinion += social_memory_weight(&bond->memories[i], tick);
    }
    return clampf(opinion, -SOCIAL_OPINION_MAX, SOCIAL_OPINION_MAX);
}

int social_reasons(const Social *social, const Colonist *colonists, int who, int whom,
                   uint64_t tick, SocialReason *out, int max)
{
    const SocialBond *bond = social_bond_const(social, who, whom);
    if (bond == NULL || max <= 0) {
        return 0;
    }
    int count = 0;
    SocialReason all[SOCIAL_REASON_MAX];
    if (bond->family != FAMILY_NONE) {
        all[count++] =
            (SocialReason){FAMILY_KEYS[bond->family], family_tie_points((FamilyTie)bond->family)};
    }
    all[count++] = (SocialReason){N_("social.reason.looks"), beauty_points(&colonists[whom])};
    all[count++] =
        (SocialReason){N_("social.reason.confidence"), confidence_points(&colonists[whom])};
    for (int i = 0; i < bond->memory_count; i++) {
        const SocialMemoryDef *def = social_memory_def((SocialMemoryType)bond->memories[i].type);
        if (def != NULL) {
            all[count++] =
                (SocialReason){def->name_key, social_memory_weight(&bond->memories[i], tick)};
        }
    }
    /* Largest first (insertion sort: a dozen at most); nothing to say below a point. */
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (fabsf(all[i].value) < 1.0f) {
            continue;
        }
        SocialReason reason = all[i];
        int at = kept++;
        while (at > 0 && fabsf(all[at - 1].value) < fabsf(reason.value)) {
            all[at] = all[at - 1];
            at--;
        }
        all[at] = reason;
    }
    if (kept > max) {
        kept = max;
    }
    memcpy(out, all, (size_t)kept * sizeof(*out));
    return kept;
}

RelationLevel social_level_of(float opinion)
{
    if (opinion >= SOCIAL_CLOSE_FRIEND_FROM) {
        return RELATION_CLOSE_FRIEND;
    }
    if (opinion >= SOCIAL_FRIEND_FROM) {
        return RELATION_FRIEND;
    }
    if (opinion <= SOCIAL_ENEMY_TO) {
        return RELATION_ENEMY;
    }
    if (opinion <= SOCIAL_RIVAL_TO) {
        return RELATION_RIVAL;
    }
    return RELATION_ACQUAINTANCE;
}

RelationLevel social_level_after(float opinion, RelationLevel previous)
{
    RelationLevel up = social_level_of(opinion - SOCIAL_LEVEL_SLACK);
    RelationLevel down = social_level_of(opinion + SOCIAL_LEVEL_SLACK);
    if (up > previous) {
        return up;
    }
    if (down < previous) {
        return down;
    }
    return previous;
}

static bool alive(const Colonist *colonists, const Pool *pool, int slot)
{
    return !entity_id_is_none(pool_id_at(pool, (uint32_t)slot)) &&
           !health_is_dead(&colonists[slot].health);
}

/* Drops the memories forgotten. */
static void forget(SocialBond *bond, uint64_t tick)
{
    int kept = 0;
    for (int i = 0; i < bond->memory_count; i++) {
        if (fabsf(social_memory_weight(&bond->memories[i], tick)) >= SOCIAL_FORGET_BELOW) {
            bond->memories[kept++] = bond->memories[i];
        }
    }
    bond->memory_count = (uint8_t)kept;
}

int social_update(Social *social, Colonist *colonists, const Pool *pool, uint64_t tick, bool notes)
{
    int changes = 0;
    int slots = (int)pool->high_water;
    for (int who = 0; who < slots; who++) {
        if (!alive(colonists, pool, who)) {
            continue;
        }
        for (int whom = 0; whom < slots; whom++) {
            if (whom == who || !alive(colonists, pool, whom)) {
                continue;
            }
            SocialBond *bond = &social->bonds[who][whom];
            forget(bond, tick);
            float opinion = social_opinion(social, colonists, who, whom, tick);
            RelationLevel previous = (RelationLevel)bond->level;
            RelationLevel level =
                notes ? social_level_after(opinion, previous) : social_level_of(opinion);
            if (level == previous) {
                continue;
            }
            bond->level = (uint8_t)level;
            changes++;
            if (notes) {
                journal_note_social(
                    &colonists[who].journal, tick, JOURNAL_RELATION, colonists[whom].name,
                    (JournalSocial){.what = (uint16_t)level, .outcome = (uint8_t)previous});
            }
        }
    }
    return changes;
}

/* The living slots, in order. Returns how many. */
static int living_slots(const Colonist *colonists, const Pool *pool, int *out)
{
    int count = 0;
    for (uint32_t slot = 0; slot < pool->high_water && count < COLONIST_MAX; slot++) {
        if (alive(colonists, pool, (int)slot)) {
            out[count++] = (int)slot;
        }
    }
    return count;
}

static FamilyTie family_of(const Social *social, int who, int whom)
{
    const SocialBond *bond = social_bond_const(social, who, whom);
    return bond != NULL ? (FamilyTie)bond->family : FAMILY_NONE;
}

static int parent_count(const Social *social, const int *slots, int count, int child)
{
    int parents = 0;
    for (int i = 0; i < count; i++) {
        parents += family_of(social, child, slots[i]) == FAMILY_PARENT;
    }
    return parents;
}

/* Ties that follow from a new one, where the ages allow and nothing is tied
 * yet: a parent's parents are grandparents, siblings share parents. */
static int follow_parent(Social *social, const Colonist *colonists, const int *slots, int count,
                         int parent, int child)
{
    int made = 0;
    for (int i = 0; i < count; i++) {
        int other = slots[i];
        if (other == parent || other == child || family_of(social, child, other) != FAMILY_NONE) {
            continue;
        }
        int gap = colonists[other].age - colonists[child].age;
        if (family_of(social, parent, other) == FAMILY_PARENT && gap >= SOCIAL_GRANDPARENT_GAP) {
            made += social_set_family(social, child, other, FAMILY_GRANDPARENT);
        } else if (family_of(social, parent, other) == FAMILY_CHILD &&
                   abs(gap) <= SOCIAL_PARENT_GAP_MAX - SOCIAL_PARENT_GAP_MIN) {
            made += social_set_family(social, child, other, FAMILY_SIBLING);
        }
    }
    return made;
}

static bool may_be_parent(const Social *social, const Colonist *colonists, const int *slots,
                          int count, int parent, int child)
{
    int gap = colonists[parent].age - colonists[child].age;
    return gap >= SOCIAL_PARENT_GAP_MIN && gap <= SOCIAL_PARENT_GAP_MAX &&
           family_of(social, child, parent) == FAMILY_NONE &&
           parent_count(social, slots, count, child) < 2;
}

int social_make_families(Social *social, Colonist *colonists, const Pool *pool)
{
    int slots[COLONIST_MAX];
    int count = living_slots(colonists, pool, slots);
    int made = 0;
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < count; j++) {
            int older = slots[i], younger = slots[j];
            int gap = colonists[older].age - colonists[younger].age;
            if (older == younger || gap < 0 || (gap == 0 && older > younger) ||
                family_of(social, older, younger) != FAMILY_NONE) {
                continue;
            }
            float roll = rng_next_float(&social->rng);
            if (may_be_parent(social, colonists, slots, count, older, younger)) {
                if (roll < SOCIAL_PARENT_CHANCE) {
                    made += social_set_family(social, younger, older, FAMILY_PARENT);
                    made += follow_parent(social, colonists, slots, count, older, younger);
                    /* The child's siblings are the parent's children too. */
                    for (int k = 0; k < count; k++) {
                        int sibling = slots[k];
                        if (sibling != younger &&
                            family_of(social, younger, sibling) == FAMILY_SIBLING &&
                            may_be_parent(social, colonists, slots, count, older, sibling)) {
                            made += social_set_family(social, sibling, older, FAMILY_PARENT);
                        }
                    }
                }
            } else if (gap <= SOCIAL_SIBLING_GAP) {
                if (roll < SOCIAL_SIBLING_CHANCE) {
                    made += social_set_family(social, older, younger, FAMILY_SIBLING);
                    /* Siblings share their parents and their siblings. */
                    for (int k = 0; k < count; k++) {
                        int other = slots[k];
                        if (other == older || other == younger) {
                            continue;
                        }
                        if (family_of(social, older, other) == FAMILY_PARENT &&
                            may_be_parent(social, colonists, slots, count, other, younger)) {
                            made += social_set_family(social, younger, other, FAMILY_PARENT);
                        } else if (family_of(social, younger, other) == FAMILY_PARENT &&
                                   may_be_parent(social, colonists, slots, count, other, older)) {
                            made += social_set_family(social, older, other, FAMILY_PARENT);
                        } else if (family_of(social, older, other) == FAMILY_SIBLING &&
                                   family_of(social, younger, other) == FAMILY_NONE) {
                            made += social_set_family(social, younger, other, FAMILY_SIBLING);
                        }
                    }
                }
            } else if (gap >= SOCIAL_GRANDPARENT_GAP && roll < SOCIAL_GRANDPARENT_CHANCE) {
                made += social_set_family(social, younger, older, FAMILY_GRANDPARENT);
            }
        }
    }
    return made;
}

void social_start_together(Social *social, Colonist *colonists, const Pool *pool, uint64_t tick)
{
    social_make_families(social, colonists, pool);
    int slots[COLONIST_MAX];
    int count = living_slots(colonists, pool, slots);
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < count; j++) {
            if (i != j) {
                float scale = SOCIAL_JOURNEY_SCALE_MIN +
                              (SOCIAL_JOURNEY_SCALE_MAX - SOCIAL_JOURNEY_SCALE_MIN) *
                                  rng_next_float(&social->rng);
                social_remember(social, slots[i], slots[j], SOCIAL_MEMORY_JOURNEY, scale, tick);
            }
        }
    }
    social_update(social, colonists, pool, tick, false);
}

ThoughtType social_grief(const Social *social, const Colonist *colonists, int mourner, int dead,
                         uint64_t tick)
{
    if (family_of(social, mourner, dead) != FAMILY_NONE) {
        return THOUGHT_FAMILY_DIED;
    }
    if (social_opinion(social, colonists, mourner, dead, tick) >= SOCIAL_FRIEND_FROM) {
        return THOUGHT_FRIEND_DIED;
    }
    return THOUGHT_DEATH;
}

void social_note_attack(Social *social, const Colonist *colonists, const Pool *pool, int attacker,
                        int victim, bool killed, uint64_t tick)
{
    if (social_bond(social, victim, attacker) == NULL) {
        return;
    }
    social_remember(social, victim, attacker, SOCIAL_MEMORY_ATTACKED_ME, 1.0f, tick);
    if (!killed) {
        return;
    }
    for (uint32_t slot = 0; slot < pool->high_water; slot++) {
        int other = (int)slot;
        if (other == victim || other == attacker || !alive(colonists, pool, other)) {
            continue;
        }
        if (family_of(social, other, victim) != FAMILY_NONE) {
            social_remember(social, other, attacker, SOCIAL_MEMORY_KILLED_FAMILY, 1.0f, tick);
        } else if (social_opinion(social, colonists, other, victim, tick) >= SOCIAL_FRIEND_FROM) {
            social_remember(social, other, attacker, SOCIAL_MEMORY_KILLED_FRIEND, 1.0f, tick);
        }
    }
}

const char *family_tie_key(FamilyTie tie)
{
    return (unsigned)tie < FAMILY_TIE_COUNT ? FAMILY_KEYS[tie] : NULL;
}

const char *relation_level_key(RelationLevel level)
{
    return (unsigned)level < RELATION_LEVEL_COUNT ? LEVEL_KEYS[level] : NULL;
}

/* ---- Tales ------------------------------------------------------------------ */

static bool slot_valid(int slot)
{
    return slot >= 0 && slot < COLONIST_MAX;
}

void social_hear_tale(Social *social, int listener, int teller, int subject, bool well,
                      uint64_t tick)
{
    if (!slot_valid(listener) || !slot_valid(teller) || !slot_valid(subject) ||
        listener == subject || teller == subject) {
        return;
    }
    SocialTale *tales = social->tales[listener];
    int slot = -1;
    for (int i = 0; i < SOCIAL_TALE_MAX && slot < 0; i++) {
        if (tales[i].teller == teller && tales[i].subject == subject) {
            slot = i; /* The same again: renewed. */
        }
    }
    for (int i = 0; i < SOCIAL_TALE_MAX && slot < 0; i++) {
        if (tales[i].teller < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < SOCIAL_TALE_MAX; i++) {
            if (tales[i].hour < tales[slot].hour) {
                slot = i;
            }
        }
    }
    tales[slot] = (SocialTale){(int8_t)teller, (int8_t)subject, well ? 1 : 0, 0, hour_of(tick)};
}

static int tale_index(const Social *social, int listener, int subject, uint64_t tick)
{
    if (!slot_valid(listener) || !slot_valid(subject)) {
        return -1;
    }
    uint32_t now = hour_of(tick);
    int best = -1;
    for (int i = 0; i < SOCIAL_TALE_MAX; i++) {
        const SocialTale *tale = &social->tales[listener][i];
        if (tale->teller < 0 || tale->subject != subject || tale->teller == subject ||
            now - tale->hour > SOCIAL_TALE_HOURS) {
            continue;
        }
        if (best < 0 || tale->hour > social->tales[listener][best].hour) {
            best = i;
        }
    }
    return best;
}

const SocialTale *social_tale_about(const Social *social, int listener, int subject, uint64_t tick)
{
    int index = tale_index(social, listener, subject, tick);
    return index >= 0 ? &social->tales[listener][index] : NULL;
}

bool social_take_tale(Social *social, int listener, int subject, uint64_t tick, SocialTale *out)
{
    int index = tale_index(social, listener, subject, tick);
    if (index < 0) {
        return false;
    }
    *out = social->tales[listener][index];
    social->tales[listener][index].teller = -1;
    return true;
}

/* ---- Apologies --------------------------------------------------------------- */

/* Memories of a wrong an apology may mend. */
static bool is_wrong(SocialMemoryType type)
{
    switch (type) {
    case SOCIAL_MEMORY_INSULTED_ME:
    case SOCIAL_MEMORY_LIED_TO_ME:
    case SOCIAL_MEMORY_ARGUED:
    case SOCIAL_MEMORY_FOUGHT:
    case SOCIAL_MEMORY_ATTACKED_ME: return true;
    default: return false;
    }
}

float social_wrongs(const Social *social, int who, int whom, uint64_t tick)
{
    const SocialBond *bond = social_bond_const(social, who, whom);
    float wrongs = 0.0f;
    for (int i = 0; bond != NULL && i < bond->memory_count; i++) {
        if (is_wrong((SocialMemoryType)bond->memories[i].type)) {
            wrongs -= social_memory_weight(&bond->memories[i], tick);
        }
    }
    return fmaxf(wrongs, 0.0f);
}

bool social_owes_apology(const Social *social, int who, int whom, uint64_t tick)
{
    if (social_wrongs(social, who, whom, tick) < SOCIAL_APOLOGY_WRONGS) {
        return false;
    }
    /* The last wrong, and the last apology (in either's memory). */
    const SocialBond *bond = social_bond_const(social, who, whom);
    const SocialBond *back = social_bond_const(social, whom, who);
    uint32_t wronged = 0, apologized = 0;
    bool any_apology = false;
    for (int i = 0; i < bond->memory_count; i++) {
        const SocialMemory *memory = &bond->memories[i];
        if (is_wrong((SocialMemoryType)memory->type) && memory->hour > wronged) {
            wronged = memory->hour;
        }
        if (memory->type == SOCIAL_MEMORY_APOLOGIZED) {
            apologized = memory->hour > apologized ? memory->hour : apologized;
            any_apology = true;
        }
    }
    for (int i = 0; i < back->memory_count; i++) {
        if (back->memories[i].type == SOCIAL_MEMORY_REFUSED_APOLOGY) {
            uint32_t hour = back->memories[i].hour;
            apologized = hour > apologized ? hour : apologized;
            any_apology = true;
        }
    }
    return !any_apology || wronged > apologized;
}

void social_forgive(Social *social, int who, int whom, float share)
{
    SocialBond *bond = social_bond(social, who, whom);
    float keep = 1.0f - clampf(share, 0.0f, 1.0f);
    for (int i = 0; bond != NULL && i < bond->memory_count; i++) {
        if (is_wrong((SocialMemoryType)bond->memories[i].type)) {
            bond->memories[i].value *= keep;
        }
    }
}

/* ---- Debug ------------------------------------------------------------------- */

static int debug_memory(const SocialBond *bond)
{
    for (int i = 0; i < bond->memory_count; i++) {
        if (bond->memories[i].type == SOCIAL_MEMORY_DEBUG) {
            return i;
        }
    }
    return -1;
}

/* Sets the debug memory of a bond to `value` points (none when about 0). */
static void set_debug_points(SocialBond *bond, float value, uint64_t tick)
{
    value = clampf(value, -SOCIAL_DEBUG_POINTS_MAX, SOCIAL_DEBUG_POINTS_MAX);
    int index = debug_memory(bond);
    if (fabsf(value) < SOCIAL_FORGET_BELOW) {
        if (index >= 0) {
            bond->memories[index] = bond->memories[--bond->memory_count];
        }
        return;
    }
    if (index < 0) {
        if (bond->memory_count < SOCIAL_MEMORY_MAX) {
            index = bond->memory_count++;
        } else { /* The memory that weighs least makes room. */
            index = 0;
            for (int i = 1; i < bond->memory_count; i++) {
                if (fabsf(social_memory_weight(&bond->memories[i], tick)) <
                    fabsf(social_memory_weight(&bond->memories[index], tick))) {
                    index = i;
                }
            }
        }
    }
    bond->memories[index] = (SocialMemory){SOCIAL_MEMORY_DEBUG, 1, 0, value, hour_of(tick)};
}

void social_debug_nudge(Social *social, int who, int whom, float points, uint64_t tick)
{
    SocialBond *bond = social_bond(social, who, whom);
    if (bond == NULL) {
        return;
    }
    int index = debug_memory(bond);
    float now = index >= 0 ? social_memory_weight(&bond->memories[index], tick) : 0.0f;
    set_debug_points(bond, now + points, tick);
}

void social_debug_set_opinion(Social *social, const Colonist *colonists, int who, int whom,
                              float target, uint64_t tick)
{
    SocialBond *bond = social_bond(social, who, whom);
    if (bond == NULL) {
        return;
    }
    /* The opinion without the debug memory, before it is clamped. */
    float rest = family_tie_points((FamilyTie)bond->family) + beauty_points(&colonists[whom]) +
                 confidence_points(&colonists[whom]);
    for (int i = 0; i < bond->memory_count; i++) {
        if (bond->memories[i].type != SOCIAL_MEMORY_DEBUG) {
            rest += social_memory_weight(&bond->memories[i], tick);
        }
    }
    set_debug_points(bond, target - rest, tick);
}

void social_debug_clear(Social *social, int slots)
{
    for (int who = 0; who < slots && who < COLONIST_MAX; who++) {
        for (int whom = 0; whom < slots && whom < COLONIST_MAX; whom++) {
            SocialBond *bond = &social->bonds[who][whom];
            int index = debug_memory(bond);
            if (index >= 0) {
                bond->memories[index] = bond->memories[--bond->memory_count];
            }
        }
    }
}

/* ---- Company ----------------------------------------------------------------- */

static bool awake_here(const Colonist *colonists, const Pool *pool, int slot)
{
    return alive(colonists, pool, slot) && !colonist_is_asleep(&colonists[slot]);
}

void social_company_update(const Social *social, Colonist *colonists, const Pool *pool)
{
    int slots = (int)pool->high_water;
    for (int who = 0; who < slots; who++) {
        if (entity_id_is_none(pool_id_at(pool, (uint32_t)who))) {
            continue;
        }
        Colonist *colonist = &colonists[who];
        colonist->social_states = 0;
        colonist->friend_slots = colonist->close_friend_slots = colonist->rival_slots = 0;
        for (int whom = 0; whom < slots && whom < COLONIST_MAX; whom++) {
            uint64_t bit = (uint64_t)1 << whom;
            RelationLevel level = (RelationLevel)social->bonds[who][whom].level;
            if (whom == who || entity_id_is_none(pool_id_at(pool, (uint32_t)whom))) {
                continue;
            }
            if (level == RELATION_CLOSE_FRIEND) {
                colonist->close_friend_slots |= bit;
            } else if (level == RELATION_FRIEND) {
                colonist->friend_slots |= bit;
            } else if (level <= RELATION_RIVAL) {
                colonist->rival_slots |= bit;
            }
        }
        if (!awake_here(colonists, pool, who)) {
            continue;
        }
        for (int whom = 0; whom < slots; whom++) {
            if (whom == who || !awake_here(colonists, pool, whom)) {
                continue;
            }
            const Colonist *other = &colonists[whom];
            int dx = other->x - colonist->x, dy = other->y - colonist->y;
            if (other->z != colonist->z ||
                dx * dx + dy * dy > SOCIAL_COMPANY_RANGE * SOCIAL_COMPANY_RANGE) {
                continue;
            }
            RelationLevel level = (RelationLevel)social->bonds[who][whom].level;
            if (level >= RELATION_FRIEND) {
                colonist->social_states |= THOUGHT_BIT(THOUGHT_GOOD_COMPANY);
            } else if (level <= RELATION_RIVAL) {
                colonist->social_states |= THOUGHT_BIT(THOUGHT_UNWELCOME_COMPANY);
            }
        }
    }
}

uint64_t social_hash(const Social *social, const Pool *pool, uint64_t hash)
{
    for (uint32_t who = 0; who < pool->high_water; who++) {
        for (uint32_t whom = 0; whom < pool->high_water; whom++) {
            const SocialBond *bond = &social->bonds[who][whom];
            if (bond->family == FAMILY_NONE && bond->memory_count == 0 &&
                bond->level == RELATION_ACQUAINTANCE) {
                continue;
            }
            uint8_t head[5] = {(uint8_t)who, (uint8_t)whom, bond->family, bond->level,
                               bond->memory_count};
            hash = hash_fnv1a64(hash, head, sizeof(head));
            for (int i = 0; i < bond->memory_count; i++) {
                const SocialMemory *memory = &bond->memories[i];
                hash = hash_fnv1a64(hash, &memory->type, sizeof(memory->type));
                hash = hash_fnv1a64(hash, &memory->value, sizeof(memory->value));
                hash = hash_fnv1a64(hash, &memory->hour, sizeof(memory->hour));
            }
        }
    }
    for (uint32_t listener = 0; listener < pool->high_water; listener++) {
        for (int i = 0; i < SOCIAL_TALE_MAX; i++) {
            const SocialTale *tale = &social->tales[listener][i];
            if (tale->teller >= 0) {
                uint8_t head[4] = {(uint8_t)listener, (uint8_t)tale->teller, (uint8_t)tale->subject,
                                   tale->well};
                hash = hash_fnv1a64(hash, head, sizeof(head));
                hash = hash_fnv1a64(hash, &tale->hour, sizeof(tale->hour));
            }
        }
    }
    return hash_fnv1a64(hash, &social->rng.state, sizeof(social->rng.state));
}

void social_serialize(Serializer *s, Social *social)
{
    for (int a = 0; a < COLONIST_MAX; a++) {
        for (int b = 0; b < COLONIST_MAX; b++) {
            SocialBond *bond = &social->bonds[a][b];
            ser_u8(s, &bond->family);
            ser_u8(s, &bond->level);
            ser_u8_below(s, &bond->memory_count, SOCIAL_MEMORY_MAX + 1);
            ser_u8(s, &bond->unused);
            for (int m = 0; m < SOCIAL_MEMORY_MAX; m++) {
                SocialMemory *memory = &bond->memories[m];
                ser_u8(s, &memory->type);
                ser_u8(s, &memory->count);
                ser_u16(s, &memory->unused);
                ser_float(s, &memory->value);
                ser_u32(s, &memory->hour);
            }
        }
        for (int t = 0; t < SOCIAL_TALE_MAX; t++) {
            SocialTale *tale = &social->tales[a][t];
            ser_i8(s, &tale->teller);
            ser_i8(s, &tale->subject);
            ser_u8(s, &tale->well);
            ser_u8(s, &tale->unused);
            ser_u32(s, &tale->hour);
        }
    }
    rng_serialize(s, &social->rng);
}
