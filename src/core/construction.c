#include "core/construction.h"

#include "core/knowledge.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

bool constructions_init(Constructions *constructions, int width, int height, int depth)
{
    memset(constructions, 0, sizeof(*constructions));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t tiles = (size_t)width * (size_t)height * (size_t)depth;
    constructions->at = calloc(tiles, sizeof(*constructions->at));
    constructions->workstation_tiles = calloc(tiles, 1);
    if (constructions->at == NULL || constructions->workstation_tiles == NULL) {
        constructions_free(constructions);
        return false;
    }
    constructions->width = width;
    constructions->height = height;
    constructions->depth = depth;
    constructions->next_number = 1;
    return true;
}

void constructions_free(Constructions *constructions)
{
    free(constructions->at);
    free(constructions->workstation_tiles);
    memset(constructions, 0, sizeof(*constructions));
}

static bool in_bounds(const Constructions *constructions, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < constructions->width && y < constructions->height &&
           z < constructions->depth;
}

static size_t index_of(const Constructions *constructions, int x, int y, int z)
{
    return ((size_t)z * (size_t)constructions->height + (size_t)y) * (size_t)constructions->width +
           (size_t)x;
}

int construction_at(const Constructions *constructions, int x, int y, int z)
{
    if (!in_bounds(constructions, x, y, z)) {
        return CONSTRUCTION_NONE;
    }
    return (int)constructions->at[index_of(constructions, x, y, z)] - 1;
}

bool construction_is_roof(const ConstructionSite *site)
{
    return site->workstation == 0 && structure_is_roof((StructureType)site->type);
}

int construction_worked_from(const Constructions *constructions, int x, int y, int z, bool overhead)
{
    int index = construction_at(constructions, x, y, overhead ? z + 1 : z);
    const ConstructionSite *site = construction_get(constructions, index);
    return site != NULL && construction_is_roof(site) == overhead ? index : CONSTRUCTION_NONE;
}

static ConstructionSite *site_get(Constructions *constructions, int index)
{
    if (index < 0 || index >= CONSTRUCTION_MAX || !constructions->list[index].used) {
        return NULL;
    }
    return &constructions->list[index];
}

const ConstructionSite *construction_get(const Constructions *constructions, int index)
{
    return site_get((Constructions *)constructions, index);
}

ConstructionSite *construction_get_mutable(Constructions *constructions, int index)
{
    return site_get(constructions, index);
}

/* construction_can_place() with, when `replacing`, a site on the tile not
 * counting. */
static bool can_place_tile(const Constructions *constructions, const Map *map, int x, int y, int z,
                           bool replacing)
{
    if (!in_bounds(constructions, x, y, z) || !map_is_passable(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    return map_is_buildable(map, x, y, z) && map->structure[index] == STRUCTURE_NONE &&
           !map_plant_blocks_building(map, x, y, z) &&
           (replacing || construction_at(constructions, x, y, z) == CONSTRUCTION_NONE);
}

bool construction_can_place(const Constructions *constructions, const Map *map, int x, int y, int z)
{
    return can_place_tile(constructions, map, x, y, z, false);
}

bool construction_can_replace(const Constructions *constructions, const Map *map, int x, int y,
                              int z)
{
    return can_place_tile(constructions, map, x, y, z, true);
}

bool construction_roof_held(const Constructions *constructions, const Map *map, int x, int y, int z)
{
    if (map_roof_supported(map, x, y, z)) {
        return true;
    }
    /* A wall planned in reach, on the level below, will hold it up. */
    for (int dy = -ROOF_SUPPORT_RANGE; dy <= ROOF_SUPPORT_RANGE; dy++) {
        for (int dx = -ROOF_SUPPORT_RANGE; dx <= ROOF_SUPPORT_RANGE; dx++) {
            const ConstructionSite *site = construction_get(
                constructions, construction_at(constructions, x + dx, y + dy, z - 1));
            if (site != NULL && !site->refresh && structure_blocks((StructureType)site->type)) {
                return true;
            }
        }
    }
    return false;
}

static bool can_place_roof(const Constructions *constructions, const Map *map, int x, int y, int z,
                           bool replacing)
{
    if (!in_bounds(constructions, x, y, z) || !map_in_bounds(map, x, y, z - 1)) {
        return false;
    }
    size_t below = map_index(map, x, y, z - 1);
    return map_is_open(map, x, y, z) && !map_has_ceiling(map, x, y, z) &&
           map_has_floor(map, x, y, z - 1) && map->rock[below] == ROCK_NONE &&
           map_roof(map, x, y, z - 1) == STRUCTURE_NONE &&
           map_climb_cost(map, x, y, z - 1) == 0.0f && /* Nor the way up of a slope or ladder. */
           (replacing || construction_at(constructions, x, y, z) == CONSTRUCTION_NONE) &&
           construction_roof_held(constructions, map, x, y, z);
}

bool construction_can_place_roof(const Constructions *constructions, const Map *map, int x, int y,
                                 int z)
{
    return can_place_roof(constructions, map, x, y, z, false);
}

static bool can_place_structure(const Constructions *constructions, const Map *map,
                                StructureType type, int x, int y, int z, Direction direction,
                                bool replacing)
{
    int length = structure_length(type);
    if (length <= 0 || (unsigned)direction >= DIRECTION_COUNT) {
        return false;
    }
    if (structure_is_roof(type)) {
        return can_place_roof(constructions, map, x, y, z, replacing);
    }
    if (structure_is_ladder(type) && !map_ladder_fits(map, x, y, z)) {
        return false; /* It would lead nowhere. */
    }
    for (int part = 0; part < length; part++) {
        if (!can_place_tile(constructions, map, x + part * direction_dx(direction),
                            y + part * direction_dy(direction), z, replacing)) {
            return false;
        }
    }
    return true;
}

bool construction_can_place_structure(const Constructions *constructions, const Map *map,
                                      StructureType type, int x, int y, int z, Direction direction)
{
    return can_place_structure(constructions, map, type, x, y, z, direction, false);
}

bool construction_can_replace_structure(const Constructions *constructions, const Map *map,
                                        StructureType type, int x, int y, int z,
                                        Direction direction)
{
    return can_place_structure(constructions, map, type, x, y, z, direction, true);
}

bool construction_is_planned(const Constructions *constructions, StructureType type, int x, int y,
                             int z, Direction direction)
{
    const ConstructionSite *site =
        construction_get(constructions, construction_at(constructions, x, y, z));
    return site != NULL && !site->refresh && site->workstation == 0 && site->type == type &&
           site->x == x && site->y == y && site->z == z &&
           (structure_length(type) == 1 || site->direction == direction);
}

bool construction_workstation_is_planned(const Constructions *constructions, WorkstationType type,
                                         int x, int y, int z)
{
    const ConstructionSite *site =
        construction_get(constructions, construction_at(constructions, x, y, z));
    return site != NULL && site->workstation == type + 1;
}

int construction_length(const ConstructionSite *site)
{
    return site->workstation != 0 ? 1 : structure_length((StructureType)site->type);
}

WorkstationType construction_workstation(const ConstructionSite *site)
{
    return site->workstation != 0 ? (WorkstationType)(site->workstation - 1)
                                  : WORKSTATION_TYPE_COUNT;
}

KnowledgeType construction_knowledge(const ConstructionSite *site)
{
    if (site->workstation != 0) {
        const WorkstationDef *def = workstation_def(construction_workstation(site));
        return def != NULL ? def->knowledge : KNOWLEDGE_NONE;
    }
    const StructureDef *def = structure_def((StructureType)site->type);
    return def != NULL ? def->knowledge : KNOWLEDGE_NONE;
}

float construction_min_level(const ConstructionSite *site)
{
    if (site->workstation != 0) {
        const WorkstationDef *def = workstation_def(construction_workstation(site));
        return def != NULL ? def->min_level : 0.0f;
    }
    const StructureDef *def = structure_def((StructureType)site->type);
    return def != NULL ? def->min_level : 0.0f;
}

const char *construction_name_key(const ConstructionSite *site)
{
    if (site->workstation != 0) {
        const WorkstationDef *def = workstation_def(construction_workstation(site));
        return def != NULL ? def->name_key : "";
    }
    const StructureDef *def = structure_def((StructureType)site->type);
    return def != NULL ? def->name_key : "";
}

void construction_tile(const ConstructionSite *site, int part, int *x, int *y, int *z)
{
    *x = site->x + part * direction_dx((Direction)site->direction);
    *y = site->y + part * direction_dy((Direction)site->direction);
    *z = site->z;
}

/* Writes `value` (site index + 1, or 0) on every tile of a site. */
static void mark_tiles(Constructions *constructions, const ConstructionSite *site, uint16_t value)
{
    for (int part = 0; part < construction_length(site); part++) {
        int x, y, z;
        construction_tile(site, part, &x, &y, &z);
        if (!in_bounds(constructions, x, y, z)) {
            continue;
        }
        constructions->at[index_of(constructions, x, y, z)] = value;
        constructions->workstation_tiles[index_of(constructions, x, y, z)] =
            value != 0 && site->workstation != 0;
    }
}

/* Takes a free slot for a site; CONSTRUCTION_NONE when the list is full. */
static int add_site(Constructions *constructions, StructureType type, int workstation, int x, int y,
                    int z, Direction direction, bool refresh)
{
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        ConstructionSite *site = &constructions->list[index];
        if (site->used) {
            continue;
        }
        *site = (ConstructionSite){.used = true,
                                   .type = (uint8_t)type,
                                   .workstation = (uint8_t)workstation,
                                   .x = (int16_t)x,
                                   .y = (int16_t)y,
                                   .z = (int16_t)z,
                                   .direction = (uint8_t)direction,
                                   .number = constructions->next_number++,
                                   .refresh = refresh};
        for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            site->delivered[slot] = (ItemStack){.type = ITEM_NONE};
        }
        mark_tiles(constructions, site, (uint16_t)(index + 1));
        constructions->used_count++;
        constructions->workstation_count += workstation != 0;
        constructions->revision++;
        return index;
    }
    return CONSTRUCTION_NONE;
}

int constructions_place(Constructions *constructions, const Map *map, StructureType type, int x,
                        int y, int z, Direction direction)
{
    if (type == STRUCTURE_NONE || (unsigned)type >= STRUCTURE_TYPE_COUNT ||
        !construction_can_place_structure(constructions, map, type, x, y, z, direction)) {
        return CONSTRUCTION_NONE;
    }
    return add_site(constructions, type, 0, x, y, z, direction, false);
}

int constructions_place_workstation(Constructions *constructions, const Map *map,
                                    WorkstationType type, int x, int y, int z)
{
    if ((unsigned)type >= WORKSTATION_TYPE_COUNT ||
        !construction_can_place(constructions, map, x, y, z)) {
        return CONSTRUCTION_NONE;
    }
    return add_site(constructions, STRUCTURE_NONE, (int)type + 1, x, y, z, DIRECTION_SOUTH, false);
}

int constructions_place_refresh(Constructions *constructions, const Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return CONSTRUCTION_NONE;
    }
    StructureType type = map_structure(map, x, y, z);
    Direction direction = map_structure_direction(map, x, y, z);
    const StructureDef *def = structure_def(type);
    if (def == NULL || def->refresh_work <= 0.0f) {
        return CONSTRUCTION_NONE;
    }
    for (int part = 0; part < def->length; part++) {
        if (construction_at(constructions, origin_x + part * direction_dx(direction),
                            origin_y + part * direction_dy(direction), z) != CONSTRUCTION_NONE) {
            return CONSTRUCTION_NONE;
        }
    }
    return add_site(constructions, type, 0, origin_x, origin_y, z, direction, true);
}

int constructions_remove(Constructions *constructions, int index,
                         ItemStack returned[STRUCTURE_MATERIAL_SLOTS])
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL) {
        return 0;
    }
    int count = 0;
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        if (site->delivered[slot].count > 0) {
            returned[count++] = site->delivered[slot];
        }
    }
    mark_tiles(constructions, site, 0);
    constructions->workstation_count -= site->workstation != 0;
    memset(site, 0, sizeof(*site));
    constructions->used_count--;
    constructions->revision++;
    return count;
}

/* The material of a slot: the structure's, or its refresh material. */
static StructureMaterial site_material(const ConstructionSite *site, int slot)
{
    StructureMaterial none = {ITEM_NONE, 0};
    if (slot < 0 || slot >= STRUCTURE_MATERIAL_SLOTS) {
        return none;
    }
    if (site->workstation != 0) {
        const WorkstationDef *station = workstation_def(construction_workstation(site));
        return station != NULL ? station->materials[slot] : none;
    }
    const StructureDef *def = structure_def((StructureType)site->type);
    if (def == NULL) {
        return none;
    }
    if (site->refresh) {
        return slot == 0 ? def->refresh_material : none;
    }
    return def->materials[slot];
}

ItemType construction_material(const ConstructionSite *site, int slot)
{
    return site_material(site, slot).type;
}

int construction_material_count(const ConstructionSite *site, int slot)
{
    StructureMaterial material = site_material(site, slot);
    return material.type != ITEM_NONE ? material.count : 0;
}

float construction_work_needed(const ConstructionSite *site)
{
    if (site->workstation != 0) {
        const WorkstationDef *station = workstation_def(construction_workstation(site));
        return station != NULL ? station->build_work : 0.0f;
    }
    const StructureDef *def = structure_def((StructureType)site->type);
    if (def == NULL) {
        return 0.0f;
    }
    return site->refresh ? def->refresh_work : def->build_work;
}

int construction_missing(const ConstructionSite *site, int slot)
{
    int missing = construction_material_count(site, slot) - (int)site->delivered[slot].count;
    return missing > 0 ? missing : 0;
}

int construction_unpromised(const ConstructionSite *site, int slot)
{
    if (slot < 0 || slot >= STRUCTURE_MATERIAL_SLOTS) {
        return 0;
    }
    int left = construction_missing(site, slot) - (int)site->promised[slot];
    return left > 0 ? left : 0;
}

int construction_promise(Constructions *constructions, int index, int slot, int count)
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL || slot < 0 || slot >= STRUCTURE_MATERIAL_SLOTS || count <= 0) {
        return 0;
    }
    int left = construction_unpromised(site, slot);
    int promised = count < left ? count : left;
    site->promised[slot] = (uint16_t)(site->promised[slot] + promised);
    return promised;
}

void construction_release_promise(Constructions *constructions, int index, int number, int slot,
                                  int count)
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL || site->number != number || slot < 0 || slot >= STRUCTURE_MATERIAL_SLOTS ||
        count <= 0) {
        return;
    }
    int promised = site->promised[slot];
    site->promised[slot] = (uint16_t)(count < promised ? promised - count : 0);
    constructions->revision++; /* Something may be left to bring. */
}

bool construction_materials_complete(const ConstructionSite *site)
{
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        if (construction_missing(site, slot) > 0) {
            return false;
        }
    }
    return true;
}

bool construction_forbidden(const ConstructionSite *site, const Map *map)
{
    if (map->forbidden_count == 0) {
        return false;
    }
    if (site->refresh) {
        return map_forbidden(map, site->x, site->y, site->z, MAP_FORBID_STRUCTURE);
    }
    for (int part = 0; part < construction_length(site); part++) {
        int x, y, z;
        construction_tile(site, part, &x, &y, &z);
        if (map_forbidden(map, x, y, z, MAP_FORBID_PLANT)) {
            return true;
        }
    }
    return false;
}

bool construction_can_work(const ConstructionSite *site, const Map *map)
{
    return construction_materials_complete(site) && !construction_forbidden(site, map) &&
           (!structure_is_roof((StructureType)site->type) ||
            map_roof_supported(map, site->x, site->y, site->z));
}

int construction_deliver(Constructions *constructions, int index, ItemStack stack)
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL || stack.count == 0) {
        return 0;
    }
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        int missing = construction_missing(site, slot);
        if (construction_material(site, slot) != stack.type || missing <= 0) {
            continue;
        }
        ItemStack taken = stack;
        taken.count = (uint16_t)(stack.count < missing ? stack.count : missing);
        ItemStack *into = &site->delivered[slot];
        if (into->count > 0) {
            /* Materials of any grade go into the same structure. */
            uint8_t lowest = into->quality < taken.quality ? into->quality : taken.quality;
            into->quality = lowest;
            taken.quality = lowest;
        }
        item_stack_merge(into, taken);
        constructions->revision++;
        return taken.count;
    }
    return 0;
}

/* A blocking structure leaves no room for items: they go next door. */
static void clear_items(Map *map, Items *items, int x, int y, int z)
{
    ItemStack stack = items_get(items, x, y, z);
    if (stack.count == 0) {
        return;
    }
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
        if (map_is_passable(map, x + DX[i], y + DY[i], z)) {
            items_remove(items, x, y, z, stack.count);
            int placed = items_drop(items, map, x + DX[i], y + DY[i], z, stack);
            if (placed < stack.count) {
                stack.count = (uint16_t)(stack.count - placed);
                items_add(items, x, y, z, stack); /* No room: what is left stays. */
            }
            return;
        }
    }
}

/* A workstation site whose work is done becomes the station. */
static bool finish_workstation(Constructions *constructions, const Map *map,
                               Workstations *workstations, int index)
{
    ConstructionSite *site = site_get(constructions, index);
    WorkstationType type = construction_workstation(site);
    int x = site->x, y = site->y, z = site->z;
    /* The site's tile is taken in the station layer by nothing yet: only a
     * plant grown meanwhile or a full list can stand in the way. */
    if (workstations == NULL || workstations->used_count >= WORKSTATION_MAX ||
        !workstation_can_place(workstations, map, x, y, z)) {
        return false;
    }
    ItemStack unused[STRUCTURE_MATERIAL_SLOTS];
    constructions_remove(constructions, index, unused); /* Used up. */
    workstations_place(workstations, map, type, x, y, z);
    return true;
}

bool construction_add_work(Constructions *constructions, Map *map, Items *items,
                           Workstations *workstations, int index, float work, double level,
                           Rng *rng)
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL || !construction_can_work(site, map)) {
        return false;
    }
    if (site->refresh &&
        (!map_structure_is_origin(map, site->x, site->y, site->z) ||
         map_structure(map, site->x, site->y, site->z) != (StructureType)site->type)) {
        return false; /* The structure is gone. */
    }
    StructureType type = (StructureType)site->type;
    float needed = construction_work_needed(site);
    site->progress = site->progress + work < needed ? site->progress + work : needed;
    if (site->progress < needed) {
        return false;
    }
    if (site->workstation != 0) {
        return finish_workstation(constructions, map, workstations, index);
    }
    int x = site->x, y = site->y, z = site->z;
    Direction direction = (Direction)site->direction;
    bool refresh = site->refresh;
    ItemQuality cap = structure_quality_cap(type, site->delivered);
    ItemStack unused[STRUCTURE_MATERIAL_SLOTS];
    constructions_remove(constructions, index, unused); /* Used up. */
    if (refresh) {
        map_set_structure_hit_points(map, x, y, z, INT16_MAX);
        return true;
    }
    ItemQuality quality = knowledge_quality_of_score(knowledge_product_score(level, rng));
    if (structure_is_roof(type)) {
        map_set_roof(map, x, y, z, type, quality < cap ? quality : cap);
        return true;
    }
    map_place_structure(map, x, y, z, type, direction, quality < cap ? quality : cap);
    for (int part = 0; structure_blocks(type) && items != NULL && part < structure_length(type);
         part++) {
        clear_items(map, items, x + part * direction_dx(direction),
                    y + part * direction_dy(direction), z);
    }
    return true;
}

bool construction_finish_now(Constructions *constructions, Map *map, Items *items,
                             Workstations *workstations, int index, double level, Rng *rng)
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL) {
        return false;
    }
    StructureType type = (StructureType)site->type;
    bool ready = true;
    if (site->workstation != 0) {
        ready = workstations != NULL && workstations->used_count < WORKSTATION_MAX &&
                workstation_can_place(workstations, map, site->x, site->y, site->z);
    } else if (site->refresh) {
        ready = map_structure_is_origin(map, site->x, site->y, site->z) &&
                map_structure(map, site->x, site->y, site->z) == type;
    } else if (structure_is_roof(type)) {
        ready = map_roof_supported(map, site->x, site->y, site->z);
    }
    if (!ready) {
        return false;
    }
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        int missing = construction_missing(site, slot);
        if (missing > 0) {
            construction_deliver(constructions, index,
                                 item_stack_make(construction_material(site, slot), missing,
                                                 ITEM_QUALITY_STANDARD, UINT16_MAX));
        }
    }
    return construction_add_work(constructions, map, items, workstations, index,
                                 construction_work_needed(site), level, rng);
}

int constructions_wear_day(Constructions *constructions, Map *map, Items *items)
{
    bool any = false;
    for (int type = STRUCTURE_NONE + 1; type < STRUCTURE_TYPE_COUNT; type++) {
        any = any || (structure_def((StructureType)type)->wear_per_day > 0 &&
                      map->structure_counts[type] > 0);
    }
    int gone = 0;
    for (int z = 0; any && z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                StructureType type = map_structure(map, x, y, z);
                int wear = structure_def(type)->wear_per_day;
                if (type == STRUCTURE_NONE || wear <= 0 || !map_structure_is_origin(map, x, y, z)) {
                    continue;
                }
                int hit_points = map_structure_hit_points(map, x, y, z) - wear;
                int site = construction_at(constructions, x, y, z);
                if (hit_points <= 0) {
                    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
                    int count = constructions_remove(constructions, site, returned);
                    for (int i = 0; items != NULL && i < count; i++) {
                        items_drop(items, map, x, y, z, returned[i]);
                    }
                    map_set_structure(map, x, y, z, STRUCTURE_NONE);
                    gone++;
                    continue;
                }
                map_set_structure_hit_points(map, x, y, z, hit_points);
                int max = structure_max_hit_points(type, map_structure_quality(map, x, y, z));
                if ((float)hit_points < (float)max * STRUCTURE_REFRESH_BELOW &&
                    site == CONSTRUCTION_NONE) {
                    constructions_place_refresh(constructions, map, x, y, z);
                }
            }
        }
    }
    return gone;
}

float construction_work_fraction(const ConstructionSite *site)
{
    float needed = construction_work_needed(site);
    return needed > 0.0f ? clampf(site->progress / needed, 0.0f, 1.0f) : 1.0f;
}

uint64_t constructions_hash(const Constructions *constructions, uint64_t hash)
{
    /* Field by field: struct padding bytes are unspecified. */
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        const ConstructionSite *site = &constructions->list[index];
        if (!site->used) {
            continue;
        }
        int32_t ints[8] = {index,        site->type,    site->x,         site->y,
                           site->number, site->refresh, site->direction, site->workstation};
        hash = hash_fnv1a64(hash, ints, sizeof(ints));
        if (site->z != 0) { /* Hashes of one-level games stay as they were. */
            hash = hash_fnv1a64(hash, &site->z, sizeof(site->z));
        }
        for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            ItemStack stack = site->delivered[slot];
            int32_t fields[5] = {stack.type, stack.quality, stack.count, stack.hit_points,
                                 site->promised[slot]};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
        }
        hash = hash_fnv1a64(hash, &site->progress, sizeof(site->progress));
        hash = hash_fnv1a64(hash, &site->clear_progress, sizeof(site->clear_progress));
    }
    return hash;
}
