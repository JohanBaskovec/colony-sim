/*
 * Experiences: the few things a settler lived through that make a story to
 * tell (docs/SOCIAL.md §5): an animal it killed, one that attacked it, a death
 * it saw, a night it nearly froze... kept newest last in a small ring, the
 * oldest forgotten. Chats draw on them (core/conversation.h), so that what a
 * settler tells is what really happened to it (or, when it lies, what did
 * not). Part of the simulation state: chats read it.
 */
#ifndef COLONY_CORE_EXPERIENCE_H
#define COLONY_CORE_EXPERIENCE_H

#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

#define EXPERIENCE_MAX 8
#define EXPERIENCE_NAME_SIZE 32
/* The same experience again within this many hours renews the last one
 * instead of filling the ring (a hunt of three hares is one story). */
#define EXPERIENCE_REPEAT_HOURS 12
/* Stories lose half their appeal every this many hours (settlers tell what
 * is fresh), never below EXPERIENCE_OLD_WEIGHT of it. */
#define EXPERIENCE_FRESH_HOURS (24 * 15)
#define EXPERIENCE_OLD_WEIGHT 0.2

typedef enum ExperienceType {
    EXPERIENCE_NONE,
    EXPERIENCE_KILLED_ANIMAL,      /* `species`, `sex`. */
    EXPERIENCE_ATTACKED_BY_ANIMAL, /* Wounded by it: `species`, `sex`. */
    EXPERIENCE_SAW_DEATH,          /* `name`. */
    EXPERIENCE_LOST_CLOSE_ONE,     /* `name`: family or a friend died. */
    EXPERIENCE_STARVED,            /* Starving. */
    EXPERIENCE_PARCHED,            /* Dying of thirst. */
    EXPERIENCE_COLD_NIGHT,         /* A night too cold to sleep. */
    EXPERIENCE_TYPE_COUNT
} ExperienceType;

typedef struct Experience {
    uint8_t type; /* ExperienceType */
    uint8_t species;
    uint8_t sex;
    uint8_t count;                   /* Times it happened (repeats folded in). */
    uint32_t hour;                   /* When it last happened (game hours). */
    char name[EXPERIENCE_NAME_SIZE]; /* Who it was about (a settler), or "". */
} Experience;

typedef struct Experiences {
    Experience list[EXPERIENCE_MAX];
    uint8_t next;  /* Where the next one goes. */
    uint8_t count; /* Up to EXPERIENCE_MAX. */
} Experiences;

void experiences_init(Experiences *experiences);

/* Something happened at `hour`: the same as a recent one (type, species and
 * name, within EXPERIENCE_REPEAT_HOURS) renews it, else it is added (the
 * oldest goes when full). `name` may be NULL. */
void experiences_add(Experiences *experiences, ExperienceType type, int species, int sex,
                     const char *name, uint32_t hour);

/* The `index`-th most recent one (0: the last), NULL past the count. */
const Experience *experiences_recent(const Experiences *experiences, int index);

/* A story worth telling at `hour`, fresh ones more likely (see
 * EXPERIENCE_FRESH_HOURS); NULL when there is none. */
const Experience *experiences_pick(const Experiences *experiences, uint32_t hour, Rng *rng);

/* Whether a story is one to boast of (a kill), the kind a liar makes up. */
bool experience_is_feat(ExperienceType type);

uint64_t experiences_hash(const Experiences *experiences, uint64_t hash);

#endif /* COLONY_CORE_EXPERIENCE_H */
