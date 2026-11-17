#include "core/known.h"

#include <stdlib.h>
#include <string.h>

bool known_init(KnownWorld *known, const Map *map)
{
    memset(known, 0, sizeof(*known));
    known->stations = calloc(map_tile_count(map), sizeof(*known->stations));
    if (known->stations == NULL || !map_init(&known->map, map->width, map->height, map->depth) ||
        !items_init(&known->items, map->width, map->height, map->depth) ||
        !dirt_init(&known->dirt, map->width, map->height, map->depth)) {
        known_free(known);
        return false;
    }
    /* Nothing is known: open space everywhere (map_init() lays a floor on level 0). */
    memset(known->map.terrain, TERRAIN_OPEN, map_tile_count(&known->map));
    known->map.surface_level = map->surface_level;
    known->map.top_ground_level = map->top_ground_level;
    return true;
}

void known_free(KnownWorld *known)
{
    map_free(&known->map);
    items_free(&known->items);
    dirt_free(&known->dirt);
    free(known->stations);
    memset(known, 0, sizeof(*known));
}

void known_copy_tile(KnownWorld *known, const Map *map, const Items *items, const Dirt *dirt,
                     const Workstations *workstations, size_t index)
{
    map_copy_tile(&known->map, map, index);
    items_copy_tile(&known->items, items, index);
    dirt_copy_tile(&known->dirt, dirt, index);
    KnownStation *picture = &known->stations[index];
    const Workstation *station =
        workstations->at[index] != 0 ? &workstations->list[workstations->at[index] - 1] : NULL;
    if (station == NULL || !station->used) {
        *picture = (KnownStation){0};
        return;
    }
    float tenths = station->fuel * 10.0f;
    picture->type_plus_one = (uint8_t)(station->type + 1);
    picture->burning = station->burning;
    picture->fuel = (uint8_t)(tenths < 0.0f ? 0.0f : tenths > 255.0f ? 255.0f : tenths);
    picture->input = station->work_input.type;
}

void known_copy_run(KnownWorld *known, const Map *map, const Items *items, const Dirt *dirt,
                    const Workstations *workstations, size_t first, size_t count)
{
    Map *dst = &known->map;
    size_t level = (size_t)dst->width * (size_t)dst->height;
    /* Counters first, from what is there and what comes. */
    for (size_t i = first; i < first + count; i++) {
        dst->slope_count += (map->slope[i] != 0) - (dst->slope[i] != 0);
        dst->structure_counts[dst->structure[i]]--;
        dst->structure_counts[map->structure[i]]++;
        dst->roof_count += (terrain_def((Terrain)map->terrain[i])->built != STRUCTURE_NONE) -
                           (terrain_def((Terrain)dst->terrain[i])->built != STRUCTURE_NONE);
        dst->forbidden_count += (map->forbidden[i] != 0) - (dst->forbidden[i] != 0);
        ItemStack *held = &known->items.stacks[i];
        if (held->type != ITEM_NONE) {
            known->items.totals[held->type] -= held->count;
            known->items.level_stacks[i / level]--;
        }
        if (items->stacks[i].type != ITEM_NONE) {
            known->items.totals[items->stacks[i].type] += items->stacks[i].count;
            known->items.level_stacks[i / level]++;
        }
        known->items.forbidden_count +=
            (items->forbidden[i] != 0) - (known->items.forbidden[i] != 0);
        known->dirt.dirty_count += (dirt->level[i] != 0) - (known->dirt.level[i] != 0);
    }
    memcpy(dst->terrain + first, map->terrain + first, count);
    memcpy(dst->rock + first, map->rock + first, count);
    memcpy(dst->slope + first, map->slope + first, count);
    memcpy(dst->plants + first, map->plants + first, count * sizeof(*map->plants));
    memcpy(dst->cobbles + first, map->cobbles + first, count);
    memcpy(dst->flint + first, map->flint + first, count);
    memcpy(dst->branches + first, map->branches + first, count);
    memcpy(dst->twigs + first, map->twigs + first, count);
    memcpy(dst->nuts + first, map->nuts + first, count);
    memcpy(dst->structure + first, map->structure + first, count);
    memcpy(dst->structure_part + first, map->structure_part + first, count);
    memcpy(dst->structure_quality + first, map->structure_quality + first, count);
    memcpy(dst->structure_hit_points + first, map->structure_hit_points + first,
           count * sizeof(*map->structure_hit_points));
    memcpy(dst->roof_quality + first, map->roof_quality + first, count);
    memcpy(dst->forbidden + first, map->forbidden + first, count);
    memcpy(known->items.stacks + first, items->stacks + first, count * sizeof(*items->stacks));
    memcpy(known->items.forbidden + first, items->forbidden + first, count);
    memcpy(known->dirt.level + first, dirt->level + first, count);
    for (size_t i = first; i < first + count; i++) {
        KnownStation *picture = &known->stations[i];
        const Workstation *station =
            workstations->at[i] != 0 ? &workstations->list[workstations->at[i] - 1] : NULL;
        if (station == NULL || !station->used) {
            *picture = (KnownStation){0};
            continue;
        }
        float tenths = station->fuel * 10.0f;
        picture->type_plus_one = (uint8_t)(station->type + 1);
        picture->burning = station->burning;
        picture->fuel = (uint8_t)(tenths < 0.0f ? 0.0f : tenths > 255.0f ? 255.0f : tenths);
        picture->input = station->work_input.type;
    }
}
