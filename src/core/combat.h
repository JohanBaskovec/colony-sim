/*
 * Combat (docs/COMBAT.md): blows between beings, for settlers and animals
 * alike. This module resolves blows; who attacks whom, chases, flees or
 * breaks off is decided by the behaviors above it (core/animal.h,
 * core/colonist.h). Injuries, bleeding, pain and death are the health
 * module's (core/health.h).
 *
 * Attackers push a strike into a CombatQueue when they are in reach of their
 * target and ready; the game resolves the queue once everyone has moved.
 */
#ifndef COLONY_CORE_COMBAT_H
#define COLONY_CORE_COMBAT_H

#include "core/serialize.h"

#include "core/health.h"
#include "core/map.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/tile_box.h"

#include <stdbool.h>
#include <stdint.h>

/* Tuning (docs/COMBAT.md). */
#define COMBAT_HIT_MIN 0.05f
#define COMBAT_HIT_MAX 0.95f
/* A blow's damage varies this much around the attack's damage. */
#define COMBAT_DAMAGE_SPREAD 0.25f
/* Agility of settlers (animals have their species'). */
#define COMBAT_SETTLER_AGILITY 0.25f
/* Strikes waiting to be resolved: more are dropped. */
#define COMBAT_QUEUE_MAX 64
/* A blow reaches this many tiles unless the attack says otherwise (adjacent,
 * diagonals included). */
#define COMBAT_REACH_ADJACENT 1
/* With no free tile around its enemy, a fighter looks again this often. */
#define COMBAT_SURROUND_RETRY_TICKS 30

typedef enum CombatantKind {
    COMBATANT_NONE,
    COMBATANT_SETTLER,
    COMBATANT_ANIMAL,
} CombatantKind;

/* Who fights: a settler or an animal, by handle. */
typedef struct Combatant {
    uint8_t kind; /* CombatantKind */
    EntityId id;
} Combatant;

static inline Combatant combatant_none(void)
{
    return (Combatant){COMBATANT_NONE, ENTITY_NONE};
}

static inline bool combatant_is_none(Combatant combatant)
{
    return combatant.kind == COMBATANT_NONE;
}

static inline bool combatant_equal(Combatant a, Combatant b)
{
    return a.kind == b.kind && (a.kind == COMBATANT_NONE || entity_id_equal(a.id, b.id));
}

typedef enum AttackType {
    ATTACK_NONE,
    ATTACK_FISTS,
    ATTACK_SPEAR_THRUST,
    ATTACK_HARDENED_SPEAR_THRUST,
    ATTACK_KICK,
    ATTACK_HOOF_STRIKE,
    ATTACK_TUSKS,
    ATTACK_WOLF_BITE,
    ATTACK_WOLF_CLAWS,
    ATTACK_FOX_BITE,
    ATTACK_FOX_CLAWS,
    /* Settlers' weapons (core/weapon.h). */
    ATTACK_STONE,                       /* A stone in the fist. */
    ATTACK_SLASH,                       /* A sharp flake. */
    ATTACK_CHOP,                        /* A chopper. */
    ATTACK_AXE_CHOP,                    /* A hand axe. */
    ATTACK_CLUB,                        /* A branch. */
    ATTACK_HEAVY_CLUB,                  /* A log, in both hands. */
    ATTACK_STAFF,                       /* A digging stick, in both hands. */
    ATTACK_HARDENED_STAFF,              /* The same, fire-hardened. */
    ATTACK_LIGHT_SPEAR_THRUST,          /* A throwing spear, in melee. */
    ATTACK_HARDENED_LIGHT_SPEAR_THRUST, /* The same, fire-hardened. */
    ATTACK_TYPE_COUNT
} AttackType;

/* The mark a blow that lands leaves for a moment on the one struck, for the eye
 * only (drawn by the frontend, core/strike_anim.h). */
typedef enum StrikeMark {
    STRIKE_MARK_BLOW,  /* A burst of short rays: blunt blows (fists, stones, clubs, kicks). */
    STRIKE_MARK_CLAWS, /* Three parallel scratches. */
    STRIKE_MARK_BITE,  /* Two toothed jaws closing. */
    STRIKE_MARK_CUT,   /* One slash along the blow (a spear, a blade, tusks). */
    STRIKE_MARK_COUNT
} StrikeMark;

typedef struct AttackDef {
    const char *id;       /* Stable identifier (saves), never shown. */
    const char *name_key; /* Translation key. */
    InjuryType injury;
    float damage;   /* Points, before the attacker's power and the defender's factor. */
    float accuracy; /* Base chance to hit, 0..1. */
    float interval; /* Seconds between two blows. */
    int reach;      /* Tiles (Chebyshev). */
    BodyLimb limb;  /* What it is made with: as good as that limb is whole. */
    StrikeMark mark;
} AttackDef;

/* NULL for ATTACK_NONE and out of range. */
const AttackDef *attack_def(AttackType type);

/* What an attack is worth, to compare: damage × accuracy ÷ interval (0 for
 * none). */
float attack_worth(AttackType type);

/* Below this ability of its limb (core/health.h), an attack cannot be made. */
#define COMBAT_LIMB_MIN 0.15f
/* Attacks a species can choose among. */
#define COMBAT_ATTACKS_MAX 3

/* One of the attacks a being can make, and how much it favors it when its
 * limb is whole (a wolf bites more than it claws). A list ends at
 * COMBAT_ATTACKS_MAX or at the first ATTACK_NONE. */
typedef struct AttackChoice {
    uint8_t attack; /* AttackType */
    float weight;
} AttackChoice;

/* Ability of the limb an attack is made with (0 for none). */
float combat_attack_ability(const Health *attacker, AttackType type);

/* How many attacks the list holds, and whether the being can make any of them
 * now: its limb is whole enough (COMBAT_LIMB_MIN) and it is not knocked out. */
int combat_choice_count(const AttackChoice choices[COMBAT_ATTACKS_MAX]);
bool combat_can_attack(const Health *attacker, const AttackChoice choices[COMBAT_ATTACKS_MAX]);

/* The longest reach among the attacks it can make now (0: none). */
int combat_choice_reach(const Health *attacker, const AttackChoice choices[COMBAT_ATTACKS_MAX]);

/*
 * The attack for this blow: drawn among those it can make, by weight times the
 * ability of their limb (a damaged limb is used less); `rng` is not touched
 * when only one can be made, and NULL picks the favorite. ATTACK_NONE when it
 * can make none. `ability` (may be NULL) gets the chosen limb's ability: the
 * blow's power is scaled by it.
 */
AttackType combat_choose_attack(const Health *attacker,
                                const AttackChoice choices[COMBAT_ATTACKS_MAX], Rng *rng,
                                float *ability);

/* Ticks between two blows of an attack. */
int attack_interval_ticks(AttackType type);

/* Whether two tiles are within `reach` of each other: the same level and no
 * further than `reach` in each direction. */
bool combat_in_reach(TilePos a, TilePos b, int reach);

/* Whether a blow from one tile reaches another: in reach, and not diagonally
 * across the corner of a wall (both tiles beside the diagonal must be
 * passable), as a step would. A NULL map: in reach is enough. */
bool combat_can_strike(const Map *map, TilePos from, TilePos to, int reach);

/* Where to stand to strike an enemy: a passable tile other than the enemy's
 * own, from which the blow reaches it (a TilePredicate, `userdata` a
 * CombatSpot). Every such tile lies in combat_spot_box(). */
typedef struct CombatSpot {
    TilePos enemy;
    int reach;
} CombatSpot;

bool combat_is_strike_spot(const Map *map, int x, int y, int z, void *userdata);
TileBox combat_spot_box(const CombatSpot *spot);

/* The defender's chance to avoid a blow: its agility times its walking
 * ability (0 for someone down: knocked out or unable to walk). */
float combat_dodge(const Health *defender, float agility);

/* The attacker's skill as the blow is struck: times its vigor (a dazed
 * attacker aims worse, docs/HEALTH.md §11.4). */
float combat_aim(float skill, const Health *attacker);

/* Chance for a blow to land. */
float combat_hit_chance(AttackType type, float skill, const Health *defender, float agility);

typedef struct BlowResult {
    bool hit;
    bool dodged;  /* A miss the defender's dodge made (it would have hit otherwise). */
    int part;     /* Part hit, -1 on a miss. */
    float damage; /* Damage taken, after the defender's factor. */
    bool killed;
} BlowResult;

/* One blow: rolls to hit, then the damage (the attack's, times `power`, times
 * its spread) on a part drawn by coverage. A dead defender is not hit. */
BlowResult combat_blow(AttackType type, float skill, float power, Health *defender, float agility,
                       Rng *rng);

/* How hurt a being is, 0..1: the larger of its pain and of the share of the
 * blood it can lose before it dies that is gone. */
float combat_hurt_fraction(const Health *health);

/* Whether a being breaks off a fight: its hurt fraction has reached `nerve`
 * (above 1: never). */
bool combat_breaks_off(const Health *health, float nerve);

/* A blow asked for, to be resolved by the game. */
typedef struct CombatStrike {
    Combatant attacker;
    Combatant target;
    uint8_t attack; /* AttackType */
    float skill;
    float power;
} CombatStrike;

typedef struct CombatQueue {
    CombatStrike list[COMBAT_QUEUE_MAX];
    int count;
} CombatQueue;

/* Whether the strike was queued (false when the queue is full). */
bool combat_queue_push(CombatQueue *queue, CombatStrike strike);

/* Blows remembered for the eye: the last ones resolved, so that the frontend
 * can play them (core/strike_anim.h). Nothing in the simulation reads them. */
#define COMBAT_LOG_MAX 128

typedef struct CombatEvent {
    uint64_t tick; /* When it was resolved. */
    Combatant attacker;
    Combatant target;
    TilePos target_at; /* Where the target stood (it may be gone since: killed). */
    uint8_t attack;    /* AttackType */
    bool hit;
} CombatEvent;

/* A ring: the oldest event is overwritten once it is full. */
typedef struct CombatLog {
    CombatEvent list[COMBAT_LOG_MAX];
    int next;  /* Where the next event goes. */
    int count; /* Up to COMBAT_LOG_MAX. */
} CombatLog;

void combat_log_push(CombatLog *log, CombatEvent event);
/* The `index`-th most recent event (0: the last one), NULL past the count. */
const CombatEvent *combat_log_recent(const CombatLog *log, int index);

/* Saves or loads (docs/SAVE.md) a combatant, and the blows waiting for the
 * end of the tick. */
void combatant_serialize(Serializer *s, Combatant *combatant);
void combat_queue_serialize(Serializer *s, CombatQueue *queue);

#endif /* COLONY_CORE_COMBAT_H */
