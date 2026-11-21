/*
 * The dead (docs/HEALTH.md §8): when a settler or a wild animal dies, its
 * body is left on the ground as an item (ITEM_SETTLER_BODY, an animal's
 * carcass) and a record of it is kept here, so that the body can say whose it
 * was and the player can still read its character, its knowledge (settlers)
 * and its journal. The body's stack points at the record (ItemStack.corpse);
 * nothing in the simulation reads the record (it is for the eye only: not
 * hashed).
 */
#ifndef COLONY_CORE_CORPSE_H
#define COLONY_CORE_CORPSE_H

#include "core/serialize.h"

#include "core/animal.h"
#include "core/colonist.h"

#include <stdbool.h>
#include <stdint.h>

/* Records kept at most (the ids are 16 bits, 0 meaning "nobody's"). */
#define CORPSES_MAX UINT16_MAX

typedef enum CorpseKind {
    CORPSE_SETTLER, /* `colonist` is set. */
    CORPSE_ANIMAL,  /* `animal` is set. */
} CorpseKind;

typedef struct Corpse {
    uint16_t id;   /* 1..CORPSES_MAX: as ItemStack.corpse. */
    uint8_t kind;  /* CorpseKind: which of the two below is valid. */
    uint64_t died; /* The tick of the death. */
    union {
        /* The settler as it was when it died: its name, age, character,
         * knowledge, health and journal. Nothing it carried (its tools and
         * load are dropped on the ground) and no path (nothing to free). */
        Colonist colonist;
        /* The animal as it was: its species, sex, character, health and
         * journal; no path. */
        Animal animal;
    };
} Corpse;

/* The records, in the order of the deaths (a record's id is its index + 1).
 * Never freed before the game ends: a body that rotted away leaves one. */
typedef struct Corpses {
    Corpse *list;
    int count;
    int capacity;
} Corpses;

void corpses_init(Corpses *corpses);
void corpses_free(Corpses *corpses);

/* Keeps a record of a settler or an animal that died at `tick`; returns its
 * id, 0 when none could be kept (too many, or no memory): the body is then
 * nobody's. */
uint16_t corpses_add_settler(Corpses *corpses, const Colonist *dead, uint64_t tick);
uint16_t corpses_add_animal(Corpses *corpses, const Animal *dead, uint64_t tick);

/* The record with this id, NULL for 0 and for an unknown id. */
const Corpse *corpses_get(const Corpses *corpses, uint16_t id);

/* Saves or loads (docs/SAVE.md) the records of the dead. */
void corpses_serialize(Serializer *s, Corpses *corpses);

#endif /* COLONY_CORE_CORPSE_H */
