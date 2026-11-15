#include "core/roof.h"

#include "core/structure.h"
#include "core/util.h"

void roof_watch_init(RoofWatch *watch, const Map *map)
{
    watch->topology_revision = map->topology_revision;
    watch->build_revision = map->build_revision;
}

/* Brings down the unsupported roofs in a rectangle (clipped to the map). */
static int collapse_area(Map *map, Items *items, Designations *designations, int x0, int y0,
                         int x1, int y1)
{
    x0 = clampi(x0, 0, map->width - 1);
    x1 = clampi(x1, 0, map->width - 1);
    y0 = clampi(y0, 0, map->height - 1);
    y1 = clampi(y1, 0, map->height - 1);
    int fallen = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            StructureType roof = map_roof(map, x, y);
            if (roof == STRUCTURE_NONE || map_roof_supported(map, x, y)) {
                continue;
            }
            map_set_roof(map, x, y, STRUCTURE_NONE, ITEM_QUALITY_CRUDE);
            const StructureDef *def = structure_def(roof);
            for (int slot = 0; items != NULL && slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
                int count = structure_salvage_count(roof, slot);
                if (count > 0) {
                    items_drop(items, map, x, y,
                               item_stack_pristine(def->materials[slot].type, count));
                }
            }
            if (designations != NULL &&
                designation_get(designations, x, y) == DESIGNATION_REMOVE_ROOF) {
                designation_set(designations, map, x, y, DESIGNATION_NONE);
            }
            fallen++;
        }
    }
    return fallen;
}

int roofs_collapse(RoofWatch *watch, Map *map, Items *items, Designations *designations)
{
    uint32_t topology = map->topology_revision - watch->topology_revision;
    uint32_t build = map->build_revision - watch->build_revision;
    int fallen = 0;
    if (map->roof_count > 0 && (topology > 0 || build > 0)) {
        bool everything = topology > MAP_TOPOLOGY_LOG_SIZE || build > MAP_BUILD_LOG_SIZE;
        for (uint32_t r = 1; r <= topology && !everything; r++) {
            uint32_t revision = watch->topology_revision + r;
            everything = map->topology_log[revision % MAP_TOPOLOGY_LOG_SIZE].anywhere;
        }
        const int reach = ROOF_SUPPORT_RANGE;
        if (everything) {
            fallen += collapse_area(map, items, designations, 0, 0, map->width - 1,
                                    map->height - 1);
        } else {
            for (uint32_t r = 1; r <= topology; r++) {
                const MapTopologyChange *change =
                    &map->topology_log[(watch->topology_revision + r) % MAP_TOPOLOGY_LOG_SIZE];
                fallen += collapse_area(map, items, designations, change->x - reach,
                                        change->y - reach, change->x + reach, change->y + reach);
            }
            for (uint32_t r = 1; r <= build; r++) {
                const MapBuildChange *change =
                    &map->build_log[(watch->build_revision + r) % MAP_BUILD_LOG_SIZE];
                fallen += collapse_area(map, items, designations, change->x - reach,
                                        change->y - reach, change->x + reach, change->y + reach);
            }
        }
    }
    /* (Roofs that fell are logged too: they hold nothing up, nothing to check.) */
    roof_watch_init(watch, map);
    return fallen;
}
