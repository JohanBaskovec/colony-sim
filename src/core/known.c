#include "core/known.h"

#include <math.h>
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
    known->items.picture = true;
    known->map.litter = &known->items; /* Its stones, branches and twigs are its items'. */
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
    /* Counters first, from what is there and what comes. */
    for (size_t i = first; i < first + count; i++) {
        dst->slope_count += (map->slope[i] != 0) - (dst->slope[i] != 0);
        dst->structure_counts[dst->structure[i]]--;
        dst->structure_counts[map->structure[i]]++;
        dst->roof_count += (terrain_def((Terrain)map->terrain[i])->built != STRUCTURE_NONE) -
                           (terrain_def((Terrain)dst->terrain[i])->built != STRUCTURE_NONE);
        dst->forbidden_count += (map->forbidden[i] != 0) - (dst->forbidden[i] != 0);
        items_copy_tile(&known->items, items, i); /* Only where they changed. */
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

static bool spot_seen(const Vision *vision, float x, float y, int z)
{
    return vision_seen(vision, (int)floorf(x), (int)floorf(y), z);
}

void known_update_animals(KnownWorld *known, const Animals *animals, const Vision *vision,
                          uint64_t tick)
{
    uint32_t slots = animals_slot_count(animals);
    for (uint32_t slot = 0; slot < ANIMAL_MAX; slot++) {
        KnownAnimal *memory = &known->animals[slot];
        EntityId id = slot < slots ? animals_id_at(animals, slot) : ENTITY_NONE;
        const Animal *animal = entity_id_is_none(id) ? NULL : &animals->list[slot];
        float x = 0.0f, y = 0.0f;
        if (animal != NULL) {
            animal_position(animal, &x, &y);
        }
        if (animal != NULL && spot_seen(vision, x, y, animal->z)) {
            *memory = (KnownAnimal){.id = id,
                                    .x = x,
                                    .y = y,
                                    .z = (int16_t)animal->z,
                                    .state = KNOWN_ANIMAL_SEEN,
                                    .species = animal->species,
                                    .sex = animal->sex,
                                    .task = animal->task,
                                    .heading = (uint8_t)animal_heading(animal),
                                    .tick = tick};
            continue;
        }
        if (memory->state == KNOWN_ANIMAL_NONE) {
            continue;
        }
        /* Not seen now: its last spot in view means it was seen leaving it, or
         * is seen gone; out of view, it is believed to be there still. */
        memory->state = spot_seen(vision, memory->x, memory->y, memory->z) ? KNOWN_ANIMAL_NONE
                                                                           : KNOWN_ANIMAL_GHOST;
    }
}

const KnownAnimal *known_animal_ghost(const KnownWorld *known, uint32_t slot)
{
    if (slot >= ANIMAL_MAX || known->animals[slot].state != KNOWN_ANIMAL_GHOST) {
        return NULL;
    }
    return &known->animals[slot];
}

const KnownAnimal *known_animal_ghost_at(const KnownWorld *known, const Map *map, float world_x,
                                         float world_y, int z, float radius)
{
    const KnownAnimal *best = NULL;
    float best_distance = radius * radius;
    for (uint32_t slot = 0; slot < ANIMAL_MAX; slot++) {
        const KnownAnimal *ghost = known_animal_ghost(known, slot);
        if (ghost == NULL || ghost->z > z ||
            (ghost->z < z &&
             map_view_level(map, (int)floorf(ghost->x), (int)floorf(ghost->y), z) != ghost->z)) {
            continue;
        }
        float distance = (ghost->x - world_x) * (ghost->x - world_x) +
                         (ghost->y - world_y) * (ghost->y - world_y);
        if (distance <= best_distance) {
            best_distance = distance;
            best = ghost;
        }
    }
    return best;
}
