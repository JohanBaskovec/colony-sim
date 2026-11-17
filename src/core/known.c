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
