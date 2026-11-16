/*
 * Roofs and ceilings falling in. Whatever lies over a tile without rock (a
 * built roof, the ground over a dug gallery) is its ceiling, held up while
 * rock or a wall (a prop...) stands within ROOF_SUPPORT_RANGE tiles on its
 * level (map_ceiling_supported()). When its last support in reach goes (a
 * wall taken apart or fallen apart, rock dug out):
 *
 * - a built roof falls in: its tile is open again, half its materials drop
 *   on the floor under it, as when it is taken down, and an order to take it
 *   down is dropped. A light roof of grass and branches hurts nobody;
 * - the ceiling of a gallery caves in: the tile fills up again (with earth,
 *   or with the rock above it); what lay on it is pushed beside it, what
 *   stood on it (a bed, a screen) is broken (half its materials beside it),
 *   and the order on it is dropped. The caller deals with what lives outside
 *   the map (sites, stations, zones) and with the settlers.
 *
 * Only tiles around what the map logged as changed (Map.build_log,
 * Map.topology_log) are checked, or every tile when too much changed at
 * once.
 */
#ifndef COLONY_CORE_ROOF_H
#define COLONY_CORE_ROOF_H

#include "core/designation.h"
#include "core/item.h"
#include "core/map.h"

#include <stdint.h>

/* The map revisions roofs were last checked at. */
typedef struct RoofWatch {
    uint32_t topology_revision;
    uint32_t build_revision;
} RoofWatch;

/* Tiles of galleries that caved in during one call (the first
 * ROOF_CAVE_IN_MAX of them; `count` counts them all). */
#define ROOF_CAVE_IN_MAX 64
typedef struct CaveIns {
    TilePos tiles[ROOF_CAVE_IN_MAX];
    int count;
} CaveIns;

/* Starts watching the map as it is now. */
void roof_watch_init(RoofWatch *watch, const Map *map);

/* Brings every roof and ceiling the map changes since the last call may have
 * left without support down (`items`, `designations` and `cave_ins` may be
 * NULL; `cave_ins` is reset first). Returns how many roofs fell in. */
int roofs_collapse(RoofWatch *watch, Map *map, Items *items, Designations *designations,
                   CaveIns *cave_ins);

#endif /* COLONY_CORE_ROOF_H */
