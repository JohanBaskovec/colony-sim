/*
 * Notices: messages the simulation raises for the player (a knowledge only
 * one settler still masters, a knowledge lost...). The game keeps the last
 * NOTICE_MAX in a ring; the frontend shows the ones it has not shown yet,
 * found by their number (0 for the first notice of the game).
 */
#ifndef COLONY_CORE_NOTICE_H
#define COLONY_CORE_NOTICE_H

#include <stdbool.h>
#include <stdint.h>

#define NOTICE_MAX 32
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

#endif /* COLONY_CORE_NOTICE_H */
