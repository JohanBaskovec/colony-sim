#include "core/roof.h"

#include "core/structure.h"
#include "core/util.h"

void roof_watch_init(RoofWatch *watch, const Map *map)
{
    watch->topology_revision = map->topology_revision;
    watch->build_revision = map->build_revision;
}

/* Drops a stack on the first passable tile beside (x, y, z), if any. */
static void drop_beside(Map *map, Items *items, int x, int y, int z, ItemStack stack)
{
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    for (int i = 0; items != NULL && stack.count > 0 && i < 4; i++) {
        if (map_is_passable(map, x + DX[i], y + DY[i], z)) {
            items_drop(items, map, x + DX[i], y + DY[i], z, stack);
            return;
        }
    }
}

/* Drops half the materials of a structure (see structure_salvage_count()). */
static void drop_salvage(Map *map, Items *items, StructureType type, int x, int y, int z,
                         bool beside)
{
    const StructureDef *def = structure_def(type);
    for (int slot = 0; items != NULL && def != NULL && slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        int count = structure_salvage_count(type, slot);
        if (count <= 0) {
            continue;
        }
        ItemStack stack = item_stack_pristine(def->materials[slot].type, count);
        if (beside) {
            drop_beside(map, items, x, y, z, stack);
        } else {
            items_drop(items, map, x, y, z, stack);
        }
    }
}

/* The ceiling over (x, y, z) caves in: the tile fills up again. */
static void cave_in(Map *map, Items *items, Designations *designations, int x, int y, int z,
                    CaveIns *cave_ins)
{
    RockType above = (RockType)map->rock[map_index(map, x, y, z + 1)];
    ItemStack lying = items != NULL ? items_get(items, x, y, z) : (ItemStack){0};
    if (lying.type != ITEM_NONE) {
        items_remove(items, x, y, z, lying.count);
    }
    StructureType structure = map_structure(map, x, y, z);
    StructureType roof = map_roof(map, x, y, z);
    map_set_structure(map, x, y, z, STRUCTURE_NONE);
    map_set_roof(map, x, y, z, STRUCTURE_NONE, ITEM_QUALITY_CRUDE);
    map_set_slope(map, x, y, z, false);
    map->plants[map_index(map, x, y, z)] = (Plant){0};
    if (designations != NULL) {
        designation_set(designations, map, x, y, z, DESIGNATION_NONE);
    }
    map_set_rock(map, x, y, z, above != ROCK_NONE ? above : ROCK_EARTH);
    drop_beside(map, items, x, y, z, lying);
    drop_salvage(map, items, structure, x, y, z, true);
    drop_salvage(map, items, roof, x, y, z, true);
    if (cave_ins != NULL) {
        if (cave_ins->count < ROOF_CAVE_IN_MAX) {
            cave_ins->tiles[cave_ins->count] = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
        }
        cave_ins->count++;
    }
}

/* Brings down the unsupported roofs and ceilings in a rectangle (clipped to
 * the map). Returns the roofs fallen. */
static int collapse_area(Map *map, Items *items, Designations *designations, int x0, int y0,
                         int x1, int y1, int z, CaveIns *cave_ins)
{
    if (z < 0 || z >= map->depth) {
        return 0;
    }
    x0 = clampi(x0, 0, map->width - 1);
    x1 = clampi(x1, 0, map->width - 1);
    y0 = clampi(y0, 0, map->height - 1);
    y1 = clampi(y1, 0, map->height - 1);
    int fallen = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            StructureType roof = map_roof(map, x, y, z);
            if (roof != STRUCTURE_NONE && !map_roof_supported(map, x, y, z)) {
                map_set_roof(map, x, y, z, STRUCTURE_NONE, ITEM_QUALITY_CRUDE);
                drop_salvage(map, items, roof, x, y, z, false);
                if (designations != NULL &&
                    designation_get(designations, x, y, z) == DESIGNATION_REMOVE_ROOF) {
                    designation_set(designations, map, x, y, z, DESIGNATION_NONE);
                }
                fallen++;
            }
            if (!map_ceiling_supported(map, x, y, z)) {
                cave_in(map, items, designations, x, y, z, cave_ins);
            }
        }
    }
    return fallen;
}

int roofs_collapse(RoofWatch *watch, Map *map, Items *items, Designations *designations,
                   CaveIns *cave_ins)
{
    uint32_t topology = map->topology_revision - watch->topology_revision;
    uint32_t build = map->build_revision - watch->build_revision;
    int fallen = 0;
    if (cave_ins != NULL) {
        cave_ins->count = 0;
    }
    if (topology > 0 || build > 0) {
        bool everything = topology > MAP_TOPOLOGY_LOG_SIZE || build > MAP_BUILD_LOG_SIZE;
        for (uint32_t r = 1; r <= topology && !everything; r++) {
            uint32_t revision = watch->topology_revision + r;
            everything = map->topology_log[revision % MAP_TOPOLOGY_LOG_SIZE].anywhere;
        }
        const int reach = ROOF_SUPPORT_RANGE;
        if (everything) {
            for (int z = 0; z < map->depth; z++) {
                fallen += collapse_area(map, items, designations, 0, 0, map->width - 1,
                                        map->height - 1, z, cave_ins);
            }
        } else {
            for (uint32_t r = 1; r <= topology; r++) {
                MapTopologyChange change =
                    map->topology_log[(watch->topology_revision + r) % MAP_TOPOLOGY_LOG_SIZE];
                fallen += collapse_area(map, items, designations, change.x - reach,
                                        change.y - reach, change.x + reach, change.y + reach,
                                        change.z, cave_ins);
            }
            for (uint32_t r = 1; r <= build; r++) {
                MapBuildChange change =
                    map->build_log[(watch->build_revision + r) % MAP_BUILD_LOG_SIZE];
                fallen += collapse_area(map, items, designations, change.x - reach,
                                        change.y - reach, change.x + reach, change.y + reach,
                                        change.z, cave_ins);
            }
        }
    }
    /* (What fell is logged too: fallen roofs hold nothing up, and a gallery
     * filled up again only holds up more.) */
    roof_watch_init(watch, map);
    return fallen;
}
