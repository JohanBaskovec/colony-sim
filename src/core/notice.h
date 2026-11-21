/*
 * Notices: messages the simulation raises for the player (a knowledge only
 * one settler still masters, a knowledge lost...). The game keeps the last
 * NOTICE_MAX in a ring; the frontend shows the ones it has not shown yet,
 * found by their number (0 for the first notice of the game).
 */
#ifndef COLONY_CORE_NOTICE_H
#define COLONY_CORE_NOTICE_H

#include "core/serialize.h"

#include <stdbool.h>
#include <stdint.h>

#define NOTICE_MAX 200 /* The colony log keeps this many. */
#define NOTICE_NAME_SIZE 32

typedef enum NoticeType {
    NOTICE_KNOWLEDGE_DISCOVERED,  /* A first settler has some idea of it. */
    NOTICE_KNOWLEDGE_MASTERED,    /* Someone masters it again (nobody did). */
    NOTICE_KNOWLEDGE_LAST_MASTER, /* Only one settler still masters it. */
    NOTICE_KNOWLEDGE_LOST,        /* Nobody masters it any more. */
    NOTICE_KNOWLEDGE_FORGOTTEN,   /* Nobody knows anything of it any more. */
    NOTICE_ROOF_COLLAPSED,        /* A roof fell in (no knowledge). */
    NOTICE_CAVE_IN,               /* A gallery's ceiling fell in (no knowledge). */
    NOTICE_TREE_FELL_ON_BUILDING, /* No side was clear for a felled tree (no knowledge). */
    /* A settler broke down (core/mood.h): sulks, goes off alone, binges. */
    NOTICE_BREAK_SULK,
    NOTICE_BREAK_WANDER_OFF,
    NOTICE_BREAK_BINGE,
    /* A settler died (docs/HEALTH.md §8): bled to death, or of a destroyed
     * vital part. */
    NOTICE_DEATH_BLOOD_LOSS,
    NOTICE_DEATH_INJURY,
    NOTICE_DEATH_STARVATION, /* Of hunger, thirst, cold or heat. */
    NOTICE_DEATH_DEHYDRATION,
    NOTICE_DEATH_HYPOTHERMIA,
    NOTICE_DEATH_HEATSTROKE,
    NOTICE_COLONY_LOST,       /* The last settler died. */
    NOTICE_SETTLER_ATTACKED,  /* An animal went for a settler (docs/COMBAT.md). */
    NOTICE_SETTLER_COLLAPSED, /* Its consciousness fell below half (docs/HEALTH.md §11). */
    /* A mobilized settler was demobilized by itself (docs/COMBAT.md §8): it
     * broke down, fled, or was knocked out. */
    NOTICE_UNDRAFTED_BREAK,
    NOTICE_UNDRAFTED_FLED,
    NOTICE_UNDRAFTED_DOWN,
    NOTICE_TYPE_COUNT
} NoticeType;

typedef struct Notice {
    uint8_t type;      /* NoticeType */
    uint8_t knowledge; /* KnowledgeType */
    /* The settler concerned (a proper noun, never translated), "" if none. */
    char name[NOTICE_NAME_SIZE];
    uint64_t tick;
} Notice;

typedef struct Notices {
    Notice list[NOTICE_MAX];
    uint32_t count; /* Notices raised since the start: the next one's number. */
} Notices;

/* Raises a notice; `name` may be NULL. The oldest is forgotten when the ring
 * is full. */
void notices_push(Notices *notices, NoticeType type, int knowledge, const char *name,
                  uint64_t tick);

/* Notice number `number`, NULL when it is not raised yet or already
 * forgotten. */
const Notice *notices_get(const Notices *notices, uint32_t number);

/* Translation key of a notice's text: {0} is the knowledge (empty for a
 * notice about none), {1} the name. */
const char *notice_text_key(NoticeType type);

/* Saves or loads (docs/SAVE.md) the colony log. */
void notices_serialize(Serializer *s, Notices *notices);

#endif /* COLONY_CORE_NOTICE_H */
