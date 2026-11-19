/*
 * The known world (docs/VISION.md): what the colony has seen, as pictures of
 * the real map, the items and the dirt of the same sizes, so that code made
 * for the real ones reads them unchanged. A tile nobody has seen is open
 * space with nothing in it; a tile seen is copied from the real world when it
 * comes into view, now and then while it is in view, and a last time when it
 * goes out of view: what the known world holds of a tile out of view is what
 * it was when it was last seen.
 *
 * Wild animals are part of that picture: one standing on a tile when the tile
 * goes out of view stays there as a ghost (KnownAnimal), until someone sees
 * the tile again or sees the animal elsewhere. One that walks out of view
 * leaves none: the colony saw it leave.
 */
#ifndef COLONY_CORE_KNOWN_H
#define COLONY_CORE_KNOWN_H

#include "core/animal.h"
#include "core/dirt.h"
#include "core/item.h"
#include "core/map.h"
#include "core/vision.h"
#include "core/workstation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the colony remembers of a workstation on a tile: what it looks like
 * (a workstation is a thing that can be destroyed, unlike the zones and orders
 * of the player, which are the colony's plans and always known). */
typedef struct KnownStation {
    uint8_t type_plus_one; /* WorkstationType + 1, 0: none. */
    bool burning;          /* A hearth's fire. */
    uint8_t fuel;          /* Tenths of a kilogram, up to 255. */
    uint8_t input;         /* ItemType put down on it to be worked (ITEM_NONE: none). */
} KnownStation;

typedef enum KnownAnimalState {
    KNOWN_ANIMAL_NONE,  /* Nothing remembered. */
    KNOWN_ANIMAL_SEEN,  /* Seen now: as it was at the last update. */
    KNOWN_ANIMAL_GHOST, /* Out of view: where it was last seen. */
} KnownAnimalState;

/* What the colony remembers of a wild animal, by its slot in Animals. */
typedef struct KnownAnimal {
    EntityId id;
    float x, y; /* Where it stood, as animal_position(). */
    int16_t z;
    uint8_t state;   /* KnownAnimalState */
    uint8_t species; /* AnimalSpecies */
    uint8_t sex;     /* AnimalSex */
    uint8_t task;    /* AnimalTask: its pose (asleep, eating...). */
    uint8_t heading; /* Heading */
    uint64_t tick;   /* When it was last seen. */
} KnownAnimal;

typedef struct KnownWorld {
    Map map;
    Items items;
    Dirt dirt;
    KnownStation *stations;          /* Per tile. */
    KnownAnimal animals[ANIMAL_MAX]; /* By slot in Animals. */
} KnownWorld;

/* A known world of the size of `map`, all unknown. */
bool known_init(KnownWorld *known, const Map *map);
void known_free(KnownWorld *known);

/* Brings the tile at `index` up to date with the real world. */
void known_copy_tile(KnownWorld *known, const Map *map, const Items *items, const Dirt *dirt,
                     const Workstations *workstations, size_t index);

/* The same for `count` consecutive tiles from `first` (a run along the layers: far
 * cheaper than tile by tile, which touches every layer's memory for each). */
void known_copy_run(KnownWorld *known, const Map *map, const Items *items, const Dirt *dirt,
                    const Workstations *workstations, size_t first, size_t count);

/* Brings the remembered animals up to date at `tick`: those seen now are
 * followed; one lost from view becomes a ghost where it was last seen if that
 * spot went out of view too, else is forgotten (it walked away in sight); a
 * ghost goes once its tile is seen again. */
void known_update_animals(KnownWorld *known, const Animals *animals, const Vision *vision,
                          uint64_t tick);

/* The ghost of the animal in `slot`, NULL when there is none. */
const KnownAnimal *known_animal_ghost(const KnownWorld *known, uint32_t slot);

/* The ghost seen from level z closest to a world point within `radius`, or
 * NULL (as game_animal_at(): on level z, or below it where `map` shows that
 * level). */
const KnownAnimal *known_animal_ghost_at(const KnownWorld *known, const Map *map, float world_x,
                                         float world_y, int z, float radius);

#endif /* COLONY_CORE_KNOWN_H */
