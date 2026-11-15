#include "core/hearth.h"

#include "core/util.h"

#include <math.h>

#define BURN_PER_TICK (HEARTH_BURN_PER_HOUR / (float)TICKS_PER_HOUR)

bool hearth_is(const Workstation *station)
{
    const WorkstationDef *def =
        station != NULL ? workstation_def((WorkstationType)station->type) : NULL;
    return def != NULL && def->hearth;
}

float hearth_fuel_value(ItemType type)
{
    const ItemDef *def = item_def(type);
    return def != NULL && type != ITEM_NONE && (HEARTH_FUEL_TYPES & ITEM_TYPE_BIT(type))
               ? def->weight
               : 0.0f;
}

float hearth_fuel_wanted(const Workstation *station)
{
    if (!hearth_is(station) || !station->keep_burning) {
        return 0.0f;
    }
    float expected = station->fuel + station->fuel_promised;
    if (expected >= HEARTH_FUEL_CAPACITY * HEARTH_REFUEL_BELOW) {
        return 0.0f;
    }
    return HEARTH_FUEL_CAPACITY - expected;
}

int hearth_fuel_items_wanted(const Workstation *station, ItemType type)
{
    float value = hearth_fuel_value(type);
    if (value <= 0.0f || !(station->fuel_types & ITEM_TYPE_BIT(type))) {
        return 0;
    }
    return (int)floorf(hearth_fuel_wanted(station) / value);
}

int hearth_add_fuel(Workstations *workstations, int index, ItemStack stack)
{
    Workstation *station = workstation_get(workstations, index);
    float value = hearth_fuel_value((ItemType)stack.type);
    if (!hearth_is(station) || value <= 0.0f || stack.count == 0) {
        return 0;
    }
    int room = (int)floorf((HEARTH_FUEL_CAPACITY - station->fuel) / value + 1e-4f);
    int taken = stack.count < room ? stack.count : room;
    if (taken <= 0) {
        return 0;
    }
    station->fuel = fminf(HEARTH_FUEL_CAPACITY, station->fuel + value * (float)taken);
    workstations->revision++;
    return taken;
}

void hearth_promise_fuel(Workstations *workstations, int index, float kilograms)
{
    Workstation *station = workstation_get(workstations, index);
    if (hearth_is(station) && kilograms > 0.0f) {
        station->fuel_promised += kilograms;
    }
}

void hearth_release_fuel(Workstations *workstations, int index, int number, float kilograms)
{
    Workstation *station = workstation_get(workstations, index);
    if (!hearth_is(station) || station->number != number || kilograms <= 0.0f) {
        return;
    }
    station->fuel_promised = fmaxf(0.0f, station->fuel_promised - kilograms);
    workstations->revision++; /* Someone else may bring it now. */
}

bool hearth_needs_lighting(const Workstation *station)
{
    return hearth_is(station) && station->keep_burning && !station->burning &&
           station->fuel > 0.0f;
}

bool hearth_light(Workstations *workstations, int index)
{
    Workstation *station = workstation_get(workstations, index);
    if (!hearth_is(station) || station->fuel <= 0.0f) {
        return false;
    }
    if (!station->burning) {
        station->burning = true;
        workstations->revision++; /* Crafts that need fire may start. */
    }
    return true;
}

bool hearth_set_keep_burning(Workstations *workstations, int index, bool keep)
{
    Workstation *station = workstation_get(workstations, index);
    if (!hearth_is(station)) {
        return false;
    }
    station->keep_burning = keep;
    workstations->revision++;
    return true;
}

bool hearth_set_fuel_type(Workstations *workstations, int index, ItemType type, bool allowed)
{
    Workstation *station = workstation_get(workstations, index);
    if (!hearth_is(station) || hearth_fuel_value(type) <= 0.0f) {
        return false;
    }
    if (allowed) {
        station->fuel_types |= ITEM_TYPE_BIT(type);
    } else {
        station->fuel_types &= ~ITEM_TYPE_BIT(type);
    }
    workstations->revision++;
    return true;
}

int hearths_burn(Workstations *workstations)
{
    int out = 0;
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        Workstation *station = &workstations->list[index];
        if (!station->used || !station->burning) {
            continue;
        }
        bool wanted_before = hearth_fuel_wanted(station) > 0.0f;
        station->fuel -= BURN_PER_TICK;
        if (station->fuel <= 0.0f) {
            station->fuel = 0.0f;
            station->burning = false;
            out++;
            workstations->revision++;
        } else if (!wanted_before && hearth_fuel_wanted(station) > 0.0f) {
            workstations->revision++; /* Haulers may bring fuel now. */
        }
    }
    return out;
}

float hearth_friction_chance(double level)
{
    float above = (float)level - KNOWLEDGE_MASTERY_LEVEL;
    return clampf(HEARTH_FRICTION_CHANCE_AT_MASTERY + HEARTH_FRICTION_CHANCE_PER_LEVEL * above,
                  0.0f, 1.0f);
}

float hearths_light_at(const Workstations *workstations, float x, float y)
{
    float light = 0.0f;
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &workstations->list[index];
        if (!station->used || !station->burning) {
            continue;
        }
        float dx = x - ((float)station->x + 0.5f), dy = y - ((float)station->y + 0.5f);
        float here = 1.0f - sqrtf(dx * dx + dy * dy) / HEARTH_LIGHT_RADIUS;
        light = fmaxf(light, here);
    }
    return clampf(light, 0.0f, 1.0f);
}

bool hearth_story_time(uint64_t tick)
{
    int hour = game_hour_from_tick(tick);
    return hour >= HEARTH_STORY_START_HOUR && hour < HEARTH_STORY_END_HOUR;
}
