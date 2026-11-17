/*
 * The known world (docs/VISION.md): what the colony has seen, as pictures of
 * the real map, the items and the dirt of the same sizes, so that code made
 * for the real ones reads them unchanged. A tile nobody has seen is open
 * space with nothing in it; a tile seen is copied from the real world when it
 * comes into view, now and then while it is in view, and a last time when it
 * goes out of view: what the known world holds of a tile out of view is what
 * it was when it was last seen.
 */
#ifndef COLONY_CORE_KNOWN_H
#define COLONY_CORE_KNOWN_H

#include "core/dirt.h"
#include "core/item.h"
#include "core/map.h"
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

typedef struct KnownWorld {
    Map map;
    Items items;
    Dirt dirt;
    KnownStation *stations; /* Per tile. */
} KnownWorld;

/* A known world of the size of `map`, all unknown. */
bool known_init(KnownWorld *known, const Map *map);
void known_free(KnownWorld *known);

/* Brings the tile at `index` up to date with the real world. */
void known_copy_tile(KnownWorld *known, const Map *map, const Items *items, const Dirt *dirt,
                     const Workstations *workstations, size_t index);

#endif /* COLONY_CORE_KNOWN_H */
