/*
 * Social life (docs/SOCIAL.md): what each settler thinks of each other one.
 *
 * An opinion goes from -100 to +100, and is not the same both ways: it is
 * what the other's family tie is worth (a parent, a sibling...), what its
 * looks and self-confidence are worth at first sight, and every memory the
 * settler keeps of the other (pleasant chats, a compliment, an insult, a lie
 * found out, the killing of a brother...). A memory weighs its full value for
 * a while (its plateau), then fades, halving every half-life: an awkward chat
 * is forgotten in days, the death of a relative at someone's hands takes
 * decades. The same memory again adds up, to a limit, and starts over.
 *
 * Opinions give relationships, from enemy to close friend, with a little
 * slack so that they do not flicker at a threshold; a settler notes in its
 * journal when one changes. Bonds are kept by colonist slot: a slot taken by
 * a newcomer, or freed, is cleared. Part of the simulation state (chats,
 * grief and loneliness read it), with a random stream of its own.
 */
#ifndef COLONY_CORE_SOCIAL_H
#define COLONY_CORE_SOCIAL_H

#include "core/serialize.h"

#include "core/colonist.h"
#include "core/pool.h"
#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

#define SOCIAL_MEMORY_MAX 8
#define SOCIAL_OPINION_MAX 100.0f
/* A memory weighing less than this (in points) is forgotten. */
#define SOCIAL_FORGET_BELOW 0.5f
/* Points of opinion of the other's beauty and self-confidence at +1 (minus
 * at -1): first impressions that last. */
#define SOCIAL_BEAUTY_POINTS 10.0f
#define SOCIAL_CONFIDENCE_POINTS 4.0f
/* Relationships: opinion from which each starts (tuning), and the slack
 * past a threshold before one changes. */
#define SOCIAL_CLOSE_FRIEND_FROM 60.0f
#define SOCIAL_FRIEND_FROM 25.0f
#define SOCIAL_RIVAL_TO -25.0f
#define SOCIAL_ENEMY_TO -60.0f
#define SOCIAL_LEVEL_SLACK 3.0f
/* The starting settlers travelled together: a good memory of each other,
 * its value times a factor drawn between these. */
#define SOCIAL_JOURNEY_SCALE_MIN 0.7f
#define SOCIAL_JOURNEY_SCALE_MAX 1.5f
/* Families among the starting settlers (tuning): the chance for a pair of
 * the right ages to be parent and child (SOCIAL_PARENT_GAP_MIN..MAX years
 * apart), siblings (at most SOCIAL_SIBLING_GAP apart) or grandparent and
 * grandchild (at least SOCIAL_GRANDPARENT_GAP apart). */
#define SOCIAL_PARENT_CHANCE 0.22f
#define SOCIAL_SIBLING_CHANCE 0.18f
#define SOCIAL_GRANDPARENT_CHANCE 0.12f
#define SOCIAL_PARENT_GAP_MIN 17
#define SOCIAL_PARENT_GAP_MAX 40
#define SOCIAL_SIBLING_GAP 12
#define SOCIAL_GRANDPARENT_GAP 36
/* What a settler heard said of others, to pass on (gossip that comes back):
 * a few tales each, forgotten after SOCIAL_TALE_HOURS. */
#define SOCIAL_TALE_MAX 4
#define SOCIAL_TALE_HOURS (24 * 10)
/* Wrongs (social_wrongs()) of at least this many points make a kind settler
 * want to apologize. */
#define SOCIAL_APOLOGY_WRONGS 4.0f
/* An accepted apology takes this share off the wrongs. */
#define SOCIAL_APOLOGY_FORGIVES 0.5f
/* A friend, or a rival, this close (tiles, straight line, same level) is
 * company a settler feels (THOUGHT_GOOD_COMPANY, THOUGHT_UNWELCOME_COMPANY). */
#define SOCIAL_COMPANY_RANGE 5

/* What the other is to a settler. Settlers have no sex: the ties do not say. */
typedef enum FamilyTie {
    FAMILY_NONE,
    FAMILY_PARENT,
    FAMILY_CHILD,
    FAMILY_SIBLING,
    FAMILY_GRANDPARENT,
    FAMILY_GRANDCHILD,
    FAMILY_TIE_COUNT
} FamilyTie;

typedef enum SocialMemoryType {
    SOCIAL_MEMORY_JOURNEY,       /* Came to this land together. */
    SOCIAL_MEMORY_PLEASANT_CHAT, /* Chats that went well (stacks). */
    SOCIAL_MEMORY_AWKWARD_CHAT,
    SOCIAL_MEMORY_TAUGHT_ME,  /* Explained a knowledge to the settler. */
    SOCIAL_MEMORY_GOOD_STORY, /* Told a story the settler liked (true or not). */
    SOCIAL_MEMORY_CONFIDED,   /* Opened up about something hard. */
    SOCIAL_MEMORY_COMPLIMENTED_ME,
    SOCIAL_MEMORY_INSULTED_ME,
    SOCIAL_MEMORY_LIED_TO_ME, /* A lie found out. */
    SOCIAL_MEMORY_GOOD_WORD,  /* Heard others speak well of them. */
    SOCIAL_MEMORY_BAD_WORD,   /* Heard others speak ill of them. */
    SOCIAL_MEMORY_ATTACKED_ME,
    SOCIAL_MEMORY_KILLED_FAMILY, /* Killed someone of the settler's family. */
    SOCIAL_MEMORY_KILLED_FRIEND,
    SOCIAL_MEMORY_SPOKE_WELL_OF_ME, /* Learned it says good things of the settler. */
    SOCIAL_MEMORY_SPOKE_ILL_OF_ME,  /* Learned it speaks ill of the settler behind its back. */
    SOCIAL_MEMORY_ARGUED,
    SOCIAL_MEMORY_FOUGHT,          /* Came to blows. */
    SOCIAL_MEMORY_APOLOGIZED,      /* Came to say sorry, and was forgiven. */
    SOCIAL_MEMORY_REFUSED_APOLOGY, /* Would not forgive the settler. */
    /* The debug tools' share of the opinion (social_debug_nudge()): its
     * points as they are set, fading only over decades. */
    SOCIAL_MEMORY_DEBUG,
    SOCIAL_MEMORY_TYPE_COUNT
} SocialMemoryType;

typedef struct SocialMemoryDef {
    const char *id;        /* Stable identifier (saves), never shown. */
    const char *name_key;  /* Why the opinion is what it is ("Pleasant chats"). */
    float value;           /* Points of opinion in full. */
    float plateau_hours;   /* Full value for this long, then fading... */
    float half_life_hours; /* ...half of it gone in this long. */
    int max_stack;         /* Adding up to this many times the value. */
} SocialMemoryDef;

typedef enum RelationLevel {
    RELATION_ENEMY,
    RELATION_RIVAL,
    RELATION_ACQUAINTANCE,
    RELATION_FRIEND,
    RELATION_CLOSE_FRIEND,
    RELATION_LEVEL_COUNT
} RelationLevel;

typedef struct SocialMemory {
    uint8_t type;  /* SocialMemoryType */
    uint8_t count; /* Times it came (for the player). */
    uint16_t unused;
    float value;   /* Points in full now (stacked, scaled): fades from `hour` on. */
    uint32_t hour; /* Game hour it was last renewed. */
} SocialMemory;

/* What a settler (`who`) thinks of another (`whom`). */
typedef struct SocialBond {
    uint8_t family; /* FamilyTie: what `whom` is to `who`. */
    uint8_t level;  /* RelationLevel last noted. */
    uint8_t memory_count;
    uint8_t unused;
    SocialMemory memories[SOCIAL_MEMORY_MAX];
} SocialBond;

/* Something a settler heard said of another (gossip), to pass on. */
typedef struct SocialTale {
    int8_t teller;  /* Slot of who said it; -1: no tale. */
    int8_t subject; /* Slot of whom it was about. */
    uint8_t well;   /* Spoke well (1) or ill (0) of them. */
    uint8_t unused;
    uint32_t hour; /* Game hour it was heard. */
} SocialTale;

typedef struct Social {
    SocialBond bonds[COLONIST_MAX][COLONIST_MAX];    /* [who][whom], by colonist slot. */
    SocialTale tales[COLONIST_MAX][SOCIAL_TALE_MAX]; /* [listener]: what it heard. */
    Rng rng;                                         /* The social stream: families, chats. */
} Social;

/* A share of an opinion, for the player: a translation key and its points. */
typedef struct SocialReason {
    const char *key;
    float value;
} SocialReason;
#define SOCIAL_REASON_MAX (SOCIAL_MEMORY_MAX + 3)

const SocialMemoryDef *social_memory_def(SocialMemoryType type);

/* Nobody knows anybody; the random stream seeded from `seed`. */
void social_init(Social *social, uint64_t seed);

/* A slot freed or taken by a newcomer: nobody thinks anything of it, nor it
 * of anybody. */
void social_clear_slot(Social *social, int slot);

/* NULL out of range or when `who` is `whom`. */
SocialBond *social_bond(Social *social, int who, int whom);
const SocialBond *social_bond_const(const Social *social, int who, int whom);

/* The tie the other way round (a parent's child...). */
FamilyTie family_tie_reverse(FamilyTie tie);

/* `whom` is `tie` to `who` (and the reverse to `whom`). */
bool social_set_family(Social *social, int who, int whom, FamilyTie tie);

/* What a memory weighs at `tick` (faded past its plateau). */
float social_memory_weight(const SocialMemory *memory, uint64_t tick);

/* `who` remembers something of `whom` at `tick`, worth `scale` times the
 * memory's value: added to the same memory (faded so far, up to max_stack
 * times its value) or new. When the bond is full, the memory that weighs
 * least goes (or the new one, when it would weigh less). */
void social_remember(Social *social, int who, int whom, SocialMemoryType type, float scale,
                     uint64_t tick);

/* What the family tie is worth. */
float family_tie_points(FamilyTie tie);

/* What `who` thinks of `whom` at `tick`, -SOCIAL_OPINION_MAX..+ (`colonists`
 * indexed by slot: the other's looks count). 0 for the same or out of range. */
float social_opinion(const Social *social, const Colonist *colonists, int who, int whom,
                     uint64_t tick);

/* The shares of an opinion, largest first: the family tie, looks,
 * confidence, then the memories (one line per kind). Returns the count. */
int social_reasons(const Social *social, const Colonist *colonists, int who, int whom,
                   uint64_t tick, SocialReason *out, int max);

/* The relationship an opinion makes, without slack. */
RelationLevel social_level_of(float opinion);

/* The relationship after `previous`, with slack: it changes once the
 * opinion is SOCIAL_LEVEL_SLACK past a threshold. */
RelationLevel social_level_after(float opinion, RelationLevel previous);

/* Bonds of the living settlers: relationships are brought up to date, and a
 * settler notes in its journal when one changed (`notes`: false for the
 * start). Memories forgotten are dropped. Returns the changes. */
int social_update(Social *social, Colonist *colonists, const Pool *pool, uint64_t tick, bool notes);

/* The starting settlers: families drawn from their ages, the journey
 * together, and their relationships, all without a journal note. */
void social_start_together(Social *social, Colonist *colonists, const Pool *pool, uint64_t tick);

/* Families among the living settlers, drawn from their ages (see
 * SOCIAL_PARENT_CHANCE...). Returns the ties made. */
int social_make_families(Social *social, Colonist *colonists, const Pool *pool);

/* The thought a death brings to `mourner`: family, a friend, or someone of
 * the colony. */
ThoughtType social_grief(const Social *social, const Colonist *colonists, int mourner, int dead,
                         uint64_t tick);

/* A settler struck another: the victim remembers it; killed, the victim's
 * family and friends remember who did it (for decades). */
void social_note_attack(Social *social, const Colonist *colonists, const Pool *pool, int attacker,
                        int victim, bool killed, uint64_t tick);

/* `listener` heard `teller` speak well or ill of `subject`: kept to pass on
 * (the same tale again is renewed; when full, the oldest goes). */
void social_hear_tale(Social *social, int listener, int teller, int subject, bool well,
                      uint64_t tick);

/* The freshest tale `listener` heard of `subject` (not from `subject`
 * itself) within SOCIAL_TALE_HOURS, NULL when none. */
const SocialTale *social_tale_about(const Social *social, int listener, int subject, uint64_t tick);

/* The same, taken out (passed on): false when none. */
bool social_take_tale(Social *social, int listener, int subject, uint64_t tick, SocialTale *out);

/* What `whom` did to `who` that an apology may mend (insults, lies found
 * out, arguments, blows), in points (0 when none): the weight of those
 * memories now. */
float social_wrongs(const Social *social, int who, int whom, uint64_t tick);

/* Whether `whom` has something to apologize to `who` for: wrongs of at least
 * SOCIAL_APOLOGY_WRONGS done since it last apologized (accepted or not). */
bool social_owes_apology(const Social *social, int who, int whom, uint64_t tick);

/* `who` forgives `whom` a `share` (0..1) of its wrongs. */
void social_forgive(Social *social, int who, int whom, float share);

/* The company each living settler awake feels now: a friend, a rival or an
 * enemy within SOCIAL_COMPANY_RANGE on its level (from the relationships last
 * noted), in Colonist.social_states; and whom it likes or dislikes, in
 * Colonist.friend_slots... (whom it seeks out when it wants company). */
void social_company_update(const Social *social, Colonist *colonists, const Pool *pool);

/* Debug: `points` more (or less) of what `who` thinks of `whom`, in the
 * bond's SOCIAL_MEMORY_DEBUG memory (made room for when the bond is full,
 * dropped when it comes back to nothing), within ±SOCIAL_DEBUG_POINTS_MAX. */
#define SOCIAL_DEBUG_POINTS_MAX 200.0f
void social_debug_nudge(Social *social, int who, int whom, float points, uint64_t tick);

/* Debug: the debug memory set so that the opinion of `who` of `whom` is
 * `target` (as far as SOCIAL_DEBUG_POINTS_MAX allows). */
void social_debug_set_opinion(Social *social, const Colonist *colonists, int who, int whom,
                              float target, uint64_t tick);

/* Debug: every debug memory of the first `slots` slots forgotten. */
void social_debug_clear(Social *social, int slots);

/* Translation keys. */
const char *family_tie_key(FamilyTie tie);
const char *relation_level_key(RelationLevel level);

uint64_t social_hash(const Social *social, const Pool *pool, uint64_t hash);

/* Saves or loads (docs/SAVE.md) every bond, tale and the stream. */
void social_serialize(Serializer *s, Social *social);

#endif /* COLONY_CORE_SOCIAL_H */
