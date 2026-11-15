/*
 * Roofs falling in. A built roof stands while a wall or rock holds it up
 * within ROOF_SUPPORT_RANGE tiles (map_roof_supported()). When its last
 * support in reach goes (a wall taken apart or fallen apart, rock dug out),
 * the roof falls in: half its materials drop on the tile, as when it is taken
 * down, and an order to take it down there is dropped. A light roof of grass
 * and branches hurts nobody.
 *
 * Only roofs around what the map logged as changed (Map.build_log,
 * Map.topology_log) are checked, or every roof when too much changed at once.
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

/* Starts watching the map as it is now. */
void roof_watch_init(RoofWatch *watch, const Map *map);

/* Brings every roof the map changes since the last call may have left
 * without support down (`items` and `designations` may be NULL). Returns
 * how many fell in. */
int roofs_collapse(RoofWatch *watch, Map *map, Items *items, Designations *designations);

#endif /* COLONY_CORE_ROOF_H */
