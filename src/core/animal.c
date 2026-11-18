#include "core/animal.h"

#include "core/i18n.h"
#include "core/item.h"
#include "core/util.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SQRT2 1.41421356f

#define PLANT_BIT(type) ((AnimalPlantMask)1 << (type))

/* Tiles around the spot a herd settles on (travel cost) where its animals
 * are placed, and tries per animal wanted before giving a species up. */
#define POPULATE_SPREAD_COST 12.0f
#define POPULATE_ATTEMPTS 30
/* Herd mates within this many tiles of one that runs off run off too. */
#define ALARM_RADIUS 16
/* An animal that has just run, or was warned, stands on the watch this long
 * (ticks) before it goes back to its life. */
#define WATCH_TICKS (3 * ANIMAL_ALERT_TICKS)
/* A follower walking back to its herd stops this many steps short of its
 * leader. */
#define HERD_STOP_SHORT 2

static const AnimalDef ANIMAL_DEFS[ANIMAL_SPECIES_COUNT] = {
    /* A browser of the wood's edge: brambles, herbs, a little grass. Alone or
     * a doe with one or two others; bucks keep to themselves. */
    [ANIMAL_ROE_DEER] = {.id = "roe_deer",
                         .name_key = N_("animal.roe_deer"),
                         .sex_name_keys = {[ANIMAL_FEMALE] = N_("animal.roe_deer.female"),
                                           [ANIMAL_MALE] = N_("animal.roe_deer.male")},
                         .walk_speed = 2.0f,
                         .flee_speed = 8.0f,
                         .hunger_hours = 24.0f,
                         .thirst_hours = 30.0f,
                         .awake_hours = 26.0f,
                         .sleep_hours = 7.0f,
                         .rest_from = 22,
                         .rest_until = 5,
                         .flight_distance = 12,
                         .herd_min = 1,
                         .herd_max = 3,
                         .male_group_max = 1,
                         .male_share = 0.35f,
                         .density = 5.0f,
                         .graze_min_growth = 0.2f,
                         .graze_bite = 0.03f,
                         .graze_value = 1.0f,
                         .plants = PLANT_BIT(PLANT_BRAMBLE) | PLANT_BIT(PLANT_PLANTAIN) |
                                   PLANT_BIT(PLANT_WILD_GARLIC) | PLANT_BIT(PLANT_NETTLE) |
                                   PLANT_BIT(PLANT_MUSHROOMS),
                         .eats_nuts = true,
                         .bite_items = 2,
                         .food_value = 1.5f,
                         .body = BODY_QUADRUPED,
                         .size = 0.7f,
                         .carcass = ITEM_ROE_DEER_CARCASS,
                         .attacks = {{ATTACK_KICK, 1.0f}, {ATTACK_HOOF_STRIKE, 1.0f}},
                         .agility = 0.45f,
                         .nerve = 0.2f,
                         .sprint_seconds = 18.0f,
                         .cold_below = -10.0f,
                         .heat_above = 34.0f},
    /* A grazer of the open ground. Hinds live in herds, stags in small
     * groups of their own. */
    [ANIMAL_RED_DEER] = {.id = "red_deer",
                         .name_key = N_("animal.red_deer"),
                         .sex_name_keys = {[ANIMAL_FEMALE] = N_("animal.red_deer.female"),
                                           [ANIMAL_MALE] = N_("animal.red_deer.male")},
                         .walk_speed = 2.0f,
                         .flee_speed = 8.0f,
                         .hunger_hours = 24.0f,
                         .thirst_hours = 24.0f,
                         .awake_hours = 26.0f,
                         .sleep_hours = 7.0f,
                         .rest_from = 22,
                         .rest_until = 5,
                         .flight_distance = 14,
                         .herd_min = 4,
                         .herd_max = 8,
                         .male_group_max = 3,
                         .male_share = 0.3f,
                         .density = 6.0f,
                         .graze_min_growth = 0.2f,
                         .graze_bite = 0.06f,
                         .graze_value = 0.5f,
                         .plants = PLANT_BIT(PLANT_BRAMBLE) | PLANT_BIT(PLANT_PLANTAIN) |
                                   PLANT_BIT(PLANT_NETTLE) | PLANT_BIT(PLANT_MUSHROOMS),
                         .eats_nuts = true,
                         .bite_items = 3,
                         .food_value = 0.5f,
                         .body = BODY_QUADRUPED,
                         .size = 1.4f,
                         .carcass = ITEM_RED_DEER_CARCASS,
                         .attacks = {{ATTACK_KICK, 1.0f}, {ATTACK_HOOF_STRIKE, 1.0f}},
                         .agility = 0.35f,
                         .nerve = 0.25f,
                         .sprint_seconds = 22.0f,
                         .cold_below = -15.0f,
                         .heat_above = 34.0f},
    /* Roots about by night for what is in the ground and under the trees:
     * roots, bulbs, nuts, mushrooms. Sows live in sounders, boars alone. */
    [ANIMAL_BOAR] =
        {.id = "boar",
         .name_key = N_("animal.boar"),
         .sex_name_keys =
             {[ANIMAL_FEMALE] = N_("animal.boar.female"), [ANIMAL_MALE] = N_("animal.boar.male")},
         .walk_speed = 1.8f,
         .flee_speed = 6.5f,
         .hunger_hours = 24.0f,
         .thirst_hours = 24.0f,
         .awake_hours = 22.0f,
         .sleep_hours = 9.0f,
         .rest_from = 8,
         .rest_until = 18,
         .flight_distance = 9,
         .herd_min = 4,
         .herd_max = 8,
         .male_group_max = 1,
         .male_share = 0.2f,
         .density = 5.0f,
         .graze_min_growth = 0.2f,
         .graze_bite = 0.05f,
         .graze_value = 0.6f,
         .plants = PLANT_BIT(PLANT_WILD_CARROT) | PLANT_BIT(PLANT_CATTAIL) |
                   PLANT_BIT(PLANT_WILD_GARLIC) | PLANT_BIT(PLANT_MUSHROOMS) |
                   PLANT_BIT(PLANT_BRAMBLE),
         .digs = true,
         .eats_nuts = true,
         .bite_items = 3,
         .food_value = 0.8f,
         .body = BODY_QUADRUPED,
         .size = 1.2f,
         .carcass = ITEM_BOAR_CARCASS,
         .attacks = {{ATTACK_TUSKS, 1.0f}},
         .agility = 0.3f,
         .nerve = 0.7f,
         .fights_back = true,
         .sprint_seconds = 12.0f,
         .cold_below = -12.0f,
         .heat_above = 35.0f},
    /* Nibbles grass and herbs at dusk and at night, alone. */
    [ANIMAL_HARE] =
        {.id = "hare",
         .name_key = N_("animal.hare"),
         .sex_name_keys =
             {[ANIMAL_FEMALE] = N_("animal.hare.female"), [ANIMAL_MALE] = N_("animal.hare.male")},
         .walk_speed = 1.6f,
         .flee_speed = 9.0f,
         .hunger_hours = 18.0f,
         .thirst_hours = 36.0f,
         .awake_hours = 24.0f,
         .sleep_hours = 7.0f,
         .rest_from = 9,
         .rest_until = 17,
         .flight_distance = 6,
         .herd_min = 1,
         .herd_max = 1,
         .male_group_max = 1,
         .male_share = 0.5f,
         .density = 8.0f,
         .graze_min_growth = 0.15f,
         .graze_bite = 0.01f,
         .graze_value = 4.0f,
         .plants = PLANT_BIT(PLANT_PLANTAIN) | PLANT_BIT(PLANT_NETTLE),
         .bite_items = 1,
         .food_value = 6.0f,
         .body = BODY_HARE,
         .size = 0.15f,
         .carcass = ITEM_HARE_CARCASS,
         .agility = 0.6f,
         .sprint_seconds = 8.0f,
         .cold_below = -10.0f,
         .heat_above = 33.0f},
    /* Hunts the deer at dusk and at night in a pack, and the hare when it can;
     * a hungry pack goes for a settler too. Eats carcasses only. */
    [ANIMAL_WOLF] =
        {.id = "wolf",
         .name_key = N_("animal.wolf"),
         .sex_name_keys =
             {[ANIMAL_FEMALE] = N_("animal.wolf.female"), [ANIMAL_MALE] = N_("animal.wolf.male")},
         .walk_speed = 2.2f,
         .flee_speed = 8.5f,
         .hunger_hours = 60.0f,
         .thirst_hours = 30.0f,
         .awake_hours = 22.0f,
         .sleep_hours = 8.0f,
         .rest_from = 9,
         .rest_until = 16,
         .flight_distance = 12,
         .herd_min = 3,
         .herd_max = 5,
         .male_group_max = 2,
         .male_share = 0.4f,
         .density = 1.5f,
         .body = BODY_CANINE,
         .size = 0.7f,
         .carcass = ITEM_WOLF_CARCASS,
         .attacks = {{ATTACK_WOLF_BITE, 3.0f}, {ATTACK_WOLF_CLAWS, 1.0f}},
         .agility = 0.4f,
         .nerve = 0.55f,
         .fights_back = true,
         .sprint_seconds = 30.0f,
         .cold_below = -25.0f,
         .heat_above = 36.0f,
         .carnivore = true,
         .prey = ANIMAL_SPECIES_BIT(ANIMAL_ROE_DEER) | ANIMAL_SPECIES_BIT(ANIMAL_RED_DEER) |
                 ANIMAL_SPECIES_BIT(ANIMAL_HARE),
         .hunts_settlers = true,
         .hunt_range = 30,
         .bite_kg = 0.6f,
         .stomach_kg = 5.0f},
    /* Stalks hares, alone. Eats carcasses only. */
    [ANIMAL_FOX] =
        {.id = "fox",
         .name_key = N_("animal.fox"),
         .sex_name_keys =
             {[ANIMAL_FEMALE] = N_("animal.fox.female"), [ANIMAL_MALE] = N_("animal.fox.male")},
         .walk_speed = 1.8f,
         .flee_speed = 7.5f,
         .hunger_hours = 36.0f,
         .thirst_hours = 36.0f,
         .awake_hours = 22.0f,
         .sleep_hours = 8.0f,
         .rest_from = 10,
         .rest_until = 17,
         .flight_distance = 9,
         .herd_min = 1,
         .herd_max = 1,
         .male_group_max = 1,
         .male_share = 0.5f,
         .density = 3.0f,
         .body = BODY_CANINE,
         .size = 0.25f,
         .carcass = ITEM_FOX_CARCASS,
         .attacks = {{ATTACK_FOX_BITE, 3.0f}, {ATTACK_FOX_CLAWS, 1.0f}},
         .agility = 0.5f,
         .nerve = 0.35f,
         .sprint_seconds = 14.0f,
         .cold_below = -22.0f,
         .heat_above = 36.0f,
         .carnivore = true,
         .prey = ANIMAL_SPECIES_BIT(ANIMAL_HARE),
         .hunt_range = 20,
         .bite_kg = 0.15f,
         .stomach_kg = 1.0f},
};

const AnimalDef *animal_def(AnimalSpecies species)
{
    return (unsigned)species < ANIMAL_SPECIES_COUNT ? &ANIMAL_DEFS[species] : NULL;
}

const char *animal_name_key(AnimalSpecies species, AnimalSex sex)
{
    if ((unsigned)species >= ANIMAL_SPECIES_COUNT || (unsigned)sex >= ANIMAL_SEX_COUNT) {
        return NULL;
    }
    return ANIMAL_DEFS[species].sex_name_keys[sex];
}

const char *animal_activity_key(const Animal *animal)
{
    bool moving = animal_is_moving(animal);
    switch ((AnimalTask)animal->task) {
    case ANIMAL_TASK_IDLE: return N_("animal.activity.idle");
    case ANIMAL_TASK_WANDER: return N_("animal.activity.wander");
    case ANIMAL_TASK_EAT: return moving ? N_("animal.activity.to_food") : N_("animal.activity.eat");
    case ANIMAL_TASK_DRINK:
        return moving ? N_("animal.activity.to_water") : N_("animal.activity.drink");
    case ANIMAL_TASK_SLEEP: return N_("animal.activity.sleep");
    case ANIMAL_TASK_FLEE: return N_("animal.activity.flee");
    case ANIMAL_TASK_ATTACK: return N_("animal.activity.attack");
    case ANIMAL_TASK_DOWN:
        return health_is_unconscious(&animal->health) ? N_("animal.activity.unconscious")
               : health_is_collapsed(&animal->health) ? N_("animal.activity.collapsed")
                                                      : N_("animal.activity.cannot_walk");
    case ANIMAL_TASK_COUNT: break;
    }
    return N_("animal.activity.idle");
}

const char *animal_need_name_key(AnimalNeed need)
{
    static const char *const KEYS[ANIMAL_NEED_COUNT] = {
        [ANIMAL_NEED_FOOD] = N_("need.food"),
        [ANIMAL_NEED_WATER] = N_("need.water"),
        [ANIMAL_NEED_REST] = N_("need.rest"),
    };
    return (unsigned)need < ANIMAL_NEED_COUNT ? KEYS[need] : NULL;
}

/* ---- Storage -------------------------------------------------------------- */

bool animals_init(Animals *animals)
{
    memset(animals, 0, sizeof(*animals));
    return pool_init(&animals->pool, ANIMAL_MAX);
}

void animals_free(Animals *animals)
{
    for (uint32_t slot = 0; slot < ANIMAL_MAX; slot++) {
        path_free(&animals->list[slot].path);
    }
    pool_free(&animals->pool);
    memset(animals, 0, sizeof(*animals));
}

uint32_t animals_slot_count(const Animals *animals)
{
    return animals->pool.high_water;
}

EntityId animals_id_at(const Animals *animals, uint32_t slot)
{
    return pool_id_at(&animals->pool, slot);
}

Animal *animals_get(Animals *animals, EntityId id)
{
    return pool_is_alive(&animals->pool, id) ? &animals->list[id.index] : NULL;
}

const Animal *animals_get_const(const Animals *animals, EntityId id)
{
    return pool_is_alive(&animals->pool, id) ? &animals->list[id.index] : NULL;
}

static int random_idle_ticks(Rng *rng)
{
    return rng_range_i32(rng, ANIMAL_IDLE_MIN_TICKS, ANIMAL_IDLE_MAX_TICKS);
}

EntityId animals_spawn(Animals *animals, const Map *map, AnimalSpecies species, AnimalSex sex,
                       int x, int y, int z, Rng *rng)
{
    if ((unsigned)species >= ANIMAL_SPECIES_COUNT || (unsigned)sex >= ANIMAL_SEX_COUNT ||
        !path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z)) {
        return ENTITY_NONE;
    }
    EntityId id = pool_alloc(&animals->pool);
    if (entity_id_is_none(id)) {
        return ENTITY_NONE;
    }
    Animal *animal = &animals->list[id.index];
    Path path = animal->path; /* Its memory is kept from one animal of the slot to the next. */
    memset(animal, 0, sizeof(*animal));
    animal->path = path;
    path_clear(&animal->path);
    animal->species = (uint8_t)species;
    animal->sex = (uint8_t)sex;
    animal->x = x;
    animal->y = y;
    animal->z = z;
    animal->task = ANIMAL_TASK_IDLE;
    animal->timer = random_idle_ticks(rng);
    animal->needs[ANIMAL_NEED_FOOD] = 0.55f + 0.4f * rng_next_float(rng);
    animal->needs[ANIMAL_NEED_WATER] = 0.6f + 0.4f * rng_next_float(rng);
    animal->needs[ANIMAL_NEED_REST] = 0.6f + 0.4f * rng_next_float(rng);
    health_init(&animal->health, ANIMAL_DEFS[species].body, ANIMAL_DEFS[species].size, 1.0f, 1.0f);
    animal->leader = id;
    animal->alert_wait = (int)(id.index % ANIMAL_ALERT_TICKS); /* Not all on the same tick. */
    animal->facing_left = rng_chance(rng, 0.5f);
    /* Its character comes from a copy of the stream, so that drawing it does
     * not shift what the others do (and the species keeps its own draws). */
    Rng character = *rng;
    Attributes attributes;
    attributes_roll_animal(&attributes, &character);
    animal_set_attributes(animal, &attributes);
    return id;
}

EntityId animals_leader(const Animals *animals, EntityId id)
{
    const Animal *animal = animals_get_const(animals, id);
    if (animal == NULL) {
        return ENTITY_NONE;
    }
    return pool_is_alive(&animals->pool, animal->leader) ? animal->leader : id;
}

bool animals_remove(Animals *animals, EntityId id)
{
    Animal *animal = animals_get(animals, id);
    if (animal == NULL) {
        return false;
    }
    /* Its herd follows the first of those who followed it. */
    EntityId heir = ENTITY_NONE;
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        EntityId other = animals_id_at(animals, slot);
        if (entity_id_is_none(other) || entity_id_equal(other, id) ||
            !entity_id_equal(animals->list[slot].leader, id)) {
            continue;
        }
        if (entity_id_is_none(heir)) {
            heir = other;
        }
        animals->list[slot].leader = heir;
    }
    Path path = animal->path;
    memset(animal, 0, sizeof(*animal));
    animal->path = path; /* Freed by animals_free(), or used again. */
    path_clear(&animal->path);
    return pool_release(&animals->pool, id);
}

bool animals_set_leader(Animals *animals, EntityId id, EntityId leader)
{
    Animal *animal = animals_get(animals, id);
    const Animal *followed = animals_get_const(animals, leader);
    if (animal == NULL || followed == NULL || animal->species != followed->species) {
        return false;
    }
    EntityId head = entity_id_equal(id, leader) ? id : animals_leader(animals, leader);
    if (entity_id_equal(head, id) && !entity_id_equal(id, leader)) {
        return true; /* It leads that herd already. */
    }
    if (entity_id_equal(id, leader)) {
        /* It leaves its herd; those who followed it follow the first of them. */
        EntityId heir = ENTITY_NONE;
        for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
            EntityId other = animals_id_at(animals, slot);
            if (entity_id_is_none(other) || entity_id_equal(other, id) ||
                !entity_id_equal(animals->list[slot].leader, id)) {
                continue;
            }
            if (entity_id_is_none(heir)) {
                heir = other;
            }
            animals->list[slot].leader = heir;
        }
        animal->leader = id;
        return true;
    }
    /* Those who followed it follow its new leader with it. */
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        if (!entity_id_is_none(animals_id_at(animals, slot)) &&
            entity_id_equal(animals->list[slot].leader, id)) {
            animals->list[slot].leader = head;
        }
    }
    animal->leader = head;
    return true;
}

int animals_herd_size(const Animals *animals, EntityId id)
{
    EntityId head = animals_leader(animals, id);
    if (entity_id_is_none(head)) {
        return 0;
    }
    int count = 0;
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        EntityId other = animals_id_at(animals, slot);
        if (!entity_id_is_none(other) && entity_id_equal(animals_leader(animals, other), head)) {
            count++;
        }
    }
    return count;
}

bool animals_on_tile(const Animals *animals, int x, int y, int z)
{
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        const Animal *animal = &animals->list[slot];
        if (!entity_id_is_none(animals_id_at(animals, slot)) && animal->x == x && animal->y == y &&
            animal->z == z) {
            return true;
        }
    }
    return false;
}

bool animal_is_moving(const Animal *animal)
{
    return animal->path_index < animal->path.count;
}

Heading animal_heading(const Animal *animal)
{
    Heading side = animal->facing_left ? HEADING_WEST : HEADING_EAST;
    if (!animal_is_moving(animal)) {
        return side;
    }
    TilePos here = {(int16_t)animal->x, (int16_t)animal->y, (int16_t)animal->z};
    return path_step_heading(here, animal->path.steps[animal->path_index], side);
}

void animal_position(const Animal *animal, float *x, float *y)
{
    float px = (float)animal->x;
    float py = (float)animal->y;
    if (animal_is_moving(animal)) {
        TilePos next = animal->path.steps[animal->path_index];
        px = lerpf(px, (float)next.x, animal->move_progress);
        py = lerpf(py, (float)next.y, animal->move_progress);
    }
    *x = px + 0.5f;
    *y = py + 0.5f;
}

/* ---- What they eat, what they fear ---------------------------------------- */

AnimalFood animal_food_at(AnimalSpecies species, const Map *map, int x, int y, int z)
{
    const AnimalDef *def = animal_def(species);
    if (def == NULL || !map_in_bounds(map, x, y, z)) {
        return ANIMAL_FOOD_NONE;
    }
    size_t index = map_index(map, x, y, z);
    const Plant *plant = &map->plants[index];
    if (plant->type != PLANT_NONE && (def->plants & PLANT_BIT(plant->type)) != 0 &&
        (def->digs || !plant_def((PlantType)plant->type)->food_dug) && plant_has_food(plant)) {
        return ANIMAL_FOOD_PLANT;
    }
    if (def->eats_nuts && map->nuts[index] > 0) {
        return ANIMAL_FOOD_NUTS;
    }
    if (plant->type == PLANT_GRASS && def->graze_value > 0.0f &&
        plant->growth >= def->graze_min_growth) {
        return ANIMAL_FOOD_GRASS;
    }
    return ANIMAL_FOOD_NONE;
}

bool animal_rest_hour(AnimalSpecies species, int hour)
{
    const AnimalDef *def = animal_def(species);
    if (def == NULL) {
        return false;
    }
    if (def->rest_from <= def->rest_until) {
        return hour >= def->rest_from && hour < def->rest_until;
    }
    return hour >= def->rest_from || hour < def->rest_until; /* Through midnight. */
}

/* Whether a tile (in bounds or not) stops the eye on a level. */
static bool blocks_sight(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return true;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] != ROCK_NONE || map->structure[index] == STRUCTURE_WALL ||
           map->structure[index] == STRUCTURE_DOOR;
}

bool animal_sees(const Map *map, int x, int y, int z, TilePos threat, int distance)
{
    int dx = threat.x - x, dy = threat.y - y, dz = threat.z - z;
    if (dz < -ANIMAL_ALERT_LEVELS || dz > ANIMAL_ALERT_LEVELS ||
        dx * dx + dy * dy > distance * distance) {
        return false;
    }
    /* A straight line over the tiles between them, on the higher of the two
     * levels: the eye goes over what is lower. */
    int level = z > threat.z ? z : threat.z;
    int step_x = dx > 0 ? 1 : -1, step_y = dy > 0 ? 1 : -1;
    int span_x = dx > 0 ? dx : -dx, span_y = dy > 0 ? dy : -dy;
    int error = span_x - span_y;
    int cx = x, cy = y;
    while (cx != threat.x || cy != threat.y) {
        int twice = 2 * error;
        if (twice > -span_y) {
            error -= span_y;
            cx += step_x;
        }
        if (twice < span_x) {
            error += span_x;
            cy += step_y;
        }
        if ((cx != threat.x || cy != threat.y) && blocks_sight(map, cx, cy, level)) {
            return false;
        }
    }
    return true;
}

/* animal_notices(), the distances times `alertness` (a dazed animal notices
 * later, docs/HEALTH.md §11.4). */
static bool notices_threat(AnimalSpecies species, const Map *map, int x, int y, int z,
                           TilePos threat, bool asleep, float alertness)
{
    const AnimalDef *def = animal_def(species);
    if (def == NULL) {
        return false;
    }
    int distance = asleep ? def->flight_distance / 2 : def->flight_distance;
    distance = (int)((float)distance * clampf(alertness, 0.0f, 1.0f) + 0.5f);
    int scent = distance / ANIMAL_SCENT_SHARE;
    int dx = threat.x - x, dy = threat.y - y, dz = threat.z - z;
    if (dz >= -1 && dz <= 1 && dx * dx + dy * dy <= scent * scent) {
        return true; /* Heard or smelled. */
    }
    return animal_sees(map, x, y, z, threat, distance);
}

bool animal_notices(AnimalSpecies species, const Map *map, int x, int y, int z, TilePos threat,
                    bool asleep)
{
    return notices_threat(species, map, x, y, z, threat, asleep, 1.0f);
}

/* Whether a settler stands within `distance` tiles of a tile (seen or not:
 * where animals do not go about their life). */
static bool near_threat(const AnimalContext *context, int x, int y, int z, int distance)
{
    for (int i = 0; i < context->threat_count; i++) {
        TilePos threat = context->threats[i];
        int dx = threat.x - x, dy = threat.y - y, dz = threat.z - z;
        if (dz >= -ANIMAL_ALERT_LEVELS && dz <= ANIMAL_ALERT_LEVELS &&
            dx * dx + dy * dy <= distance * distance) {
            return true;
        }
    }
    return false;
}

/* ---- Searches ------------------------------------------------------------- */

static TilePos tile_of(const Animal *animal)
{
    return (TilePos){(int16_t)animal->x, (int16_t)animal->y, (int16_t)animal->z};
}

static bool find_nearest(const AnimalContext *context, TilePos start, TilePredicate predicate,
                         void *userdata, float max_cost, Path *out, TilePos *found)
{
    context->pathfinder->walker = PATH_WALKER_ANIMAL;
    bool ok = pathfinder_find_nearest(context->pathfinder, context->map, start, predicate, userdata,
                                      max_cost, out, found);
    context->pathfinder->walker = PATH_WALKER_SETTLER;
    return ok;
}

static bool find_path(const AnimalContext *context, TilePos start, TilePos goal, float max_cost,
                      Path *out)
{
    context->pathfinder->walker = PATH_WALKER_ANIMAL;
    bool ok = pathfinder_find(context->pathfinder, context->map, start, goal, max_cost, out);
    context->pathfinder->walker = PATH_WALKER_SETTLER;
    return ok;
}

/* Whether it may stop on the tile (nobody else holds it, core/occupancy.h). */
static bool may_stop_at(const AnimalContext *context, TilePos tile)
{
    return context->occupancy == NULL ||
           occupancy_can_stop(context->occupancy, context->self, tile);
}

/* What a search looks for, and for whom. */
typedef struct Seeker {
    const AnimalContext *context;
    AnimalSpecies species;
    bool treats_only; /* Food: nothing less than fruit, roots or nuts. */
    int clearance;    /* Tiles to keep from every settler. */
    bool has_enemy;   /* ...and from where its enemy was last seen. */
    TilePos enemy;
} Seeker;

/* Whether a settler, or the seeker's enemy, is within the seeker's clearance of a tile. */
static bool near_seeker(const Seeker *seeker, int x, int y, int z)
{
    if (near_threat(seeker->context, x, y, z, seeker->clearance)) {
        return true;
    }
    int dx = seeker->enemy.x - x, dy = seeker->enemy.y - y, dz = seeker->enemy.z - z;
    return seeker->has_enemy && dz >= -ANIMAL_ALERT_LEVELS && dz <= ANIMAL_ALERT_LEVELS &&
           dx * dx + dy * dy <= seeker->clearance * seeker->clearance;
}

/* Whether food lying on a tile is within reach of one standing on another:
 * the same tile, or one beside it with a direct step between them (not
 * diagonally across a wall's corner, not from another level). */
static bool within_bite(const Map *map, TilePos from, TilePos food)
{
    if (from.x == food.x && from.y == food.y && from.z == food.z) {
        return true;
    }
    return from.z == food.z && path_walker_can_step(PATH_WALKER_ANIMAL, map, from, food);
}

typedef bool (*FoodCheck)(const Seeker *seeker, const Map *map, int x, int y, int z);

/* Food beside a tile, on a tile someone else stops on (busy there: eating
 * it, say), within bite of the tile: where one eats from around it. */
static bool food_beside(const Seeker *seeker, const Map *map, TilePos at, FoodCheck check,
                        TilePos *food)
{
    const Occupancy *occupancy = seeker->context->occupancy;
    if (occupancy == NULL) {
        return false;
    }
    OccupancyStopper stopper = {occupancy, seeker->context->self};
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            TilePos next = {(int16_t)(at.x + dx), (int16_t)(at.y + dy), at.z};
            if ((dx == 0 && dy == 0) || !map_in_bounds(map, next.x, next.y, next.z) ||
                occupancy_stop_filter(map, next.x, next.y, next.z, &stopper) ||
                !check(seeker, map, next.x, next.y, next.z) || !within_bite(map, at, next)) {
                continue;
            }
            *food = next;
            return true;
        }
    }
    return false;
}

/* Where the food is for one that stops on `at` (found by a search with a
 * predicate below): there, or beside it. */
static TilePos food_for(const Seeker *seeker, const Map *map, TilePos at, FoodCheck check)
{
    TilePos food = at;
    if (!check(seeker, map, at.x, at.y, at.z)) {
        food_beside(seeker, map, at, check, &food);
    }
    return food;
}

static bool food_on(const Seeker *seeker, const Map *map, int x, int y, int z)
{
    AnimalFood food = animal_food_at(seeker->species, map, x, y, z);
    return (seeker->treats_only ? food > ANIMAL_FOOD_GRASS : food != ANIMAL_FOOD_NONE) &&
           !near_seeker(seeker, x, y, z);
}

/* A tile with food, or (the food's own tile taken) one to eat it from. */
static bool is_food(const Map *map, int x, int y, int z, void *userdata)
{
    const Seeker *seeker = userdata;
    TilePos food;
    return food_on(seeker, map, x, y, z) ||
           (path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z) &&
            food_beside(seeker, map, (TilePos){(int16_t)x, (int16_t)y, (int16_t)z}, food_on,
                        &food));
}

static bool is_water(const Map *map, int x, int y, int z, void *userdata)
{
    const Seeker *seeker = userdata;
    return map_is_drinking_spot(map, x, y, z) && !near_seeker(seeker, x, y, z);
}

static bool is_refuge(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const Seeker *seeker = userdata;
    return !near_seeker(seeker, x, y, z);
}

static void search_failed(Animal *animal, AnimalSearch search)
{
    if (animal->search_failures[search] < ANIMAL_SEARCH_MAX_FAILURES) {
        animal->search_failures[search]++;
    }
    animal->search_cooldown[search] = ANIMAL_SEARCH_RETRY_TICKS * animal->search_failures[search];
}

/* ---- Tasks ---------------------------------------------------------------- */

/* Stands where it is for `ticks`, then decides again. */
static void stand(Animal *animal, int ticks)
{
    animal->task = ANIMAL_TASK_IDLE;
    animal->timer = ticks;
    animal->path_index = 0;
    path_clear(&animal->path);
}

static void begin(Animal *animal, AnimalTask task, TilePos target)
{
    animal->task = (uint8_t)task;
    animal->target = target;
    animal->timer = 0;
    animal->path_index = 0;
}

/* Goes for plants, nuts or grass (the herbivores' food). */
static bool try_forage(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    if (animal->search_cooldown[ANIMAL_SEARCH_FOOD] > 0) {
        return false;
    }
    Seeker seeker = {context, (AnimalSpecies)animal->species, true, def->flight_distance, false,
                     {0}};
    TilePos found;
    bool ok = (def->plants != 0 || def->eats_nuts) &&
              find_nearest(context, tile_of(animal), is_food, &seeker, ANIMAL_TREAT_SEARCH_MAX_COST,
                           &animal->path, &found);
    if (!ok) {
        seeker.treats_only = false;
        ok = find_nearest(context, tile_of(animal), is_food, &seeker, ANIMAL_FOOD_SEARCH_MAX_COST,
                          &animal->path, &found);
    }
    if (!ok) {
        path_clear(&animal->path);
        search_failed(animal, ANIMAL_SEARCH_FOOD);
        return false;
    }
    animal->search_failures[ANIMAL_SEARCH_FOOD] = 0;
    begin(animal, ANIMAL_TASK_EAT, food_for(&seeker, context->map, found, food_on));
    return true;
}

/* ---- Hunting -------------------------------------------------------------- */

/* A carcass an animal can eat: any animal's, not a settler's. */
static bool carcass_edible(ItemType type)
{
    const ItemDef *def = item_def(type);
    return type != ITEM_NONE && def != NULL && def->category == ITEM_CATEGORY_BODIES &&
           type != ITEM_SETTLER_BODY;
}

static bool carcass_on(const Seeker *seeker, const Map *map, int x, int y, int z)
{
    (void)map;
    return carcass_edible((ItemType)items_get(seeker->context->items, x, y, z).type) &&
           !near_seeker(seeker, x, y, z);
}

/* A tile with a carcass, or (the carcass's own tile taken) one to eat it from. */
static bool is_carcass(const Map *map, int x, int y, int z, void *userdata)
{
    const Seeker *seeker = userdata;
    TilePos food;
    return carcass_on(seeker, map, x, y, z) ||
           (path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z) &&
            food_beside(seeker, map, (TilePos){(int16_t)x, (int16_t)y, (int16_t)z}, carcass_on,
                        &food));
}

static void start_attack(Animal *animal, Combatant enemy);

/* The nearest prey in sight (the species it hunts, or a settler when it is
 * desperate: counted twice as far). False when there is none. */
static bool pick_prey(const Animals *animals, uint32_t slot, const AnimalContext *context,
                      Combatant *target)
{
    const Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    int range = def->hunt_range;
    long best = LONG_MAX; /* Squared distance, a settler's counted twice as far. */
    bool found = false;
    for (uint32_t other = 0; other < animals_slot_count(animals); other++) {
        EntityId id = animals_id_at(animals, other);
        const Animal *prey = &animals->list[other];
        if (other == slot || entity_id_is_none(id) || health_is_dead(&prey->health) ||
            (def->prey & ANIMAL_SPECIES_BIT(prey->species)) == 0) {
            continue;
        }
        long dx = prey->x - animal->x, dy = prey->y - animal->y;
        if (dx * dx + dy * dy <= (long)range * range && dx * dx + dy * dy < best &&
            animal_sees(context->map, animal->x, animal->y, animal->z, tile_of(prey), range)) {
            best = dx * dx + dy * dy;
            *target = (Combatant){COMBATANT_ANIMAL, id};
            found = true;
        }
    }
    if (def->hunts_settlers && animal->needs[ANIMAL_NEED_FOOD] < ANIMAL_DESPERATE) {
        for (int i = 0; i < context->settler_count; i++) {
            TilePos at = context->settlers[i].pos;
            long dx = at.x - animal->x, dy = at.y - animal->y;
            if (dx * dx + dy * dy <= (long)range * range && 4 * (dx * dx + dy * dy) < best &&
                animal_sees(context->map, animal->x, animal->y, animal->z, at, range)) {
                best = 4 * (dx * dx + dy * dy);
                *target = (Combatant){COMBATANT_SETTLER, context->settlers[i].id};
                found = true;
            }
        }
    }
    return found;
}

/* The animal and its herd mates close by go for `target`. */
static void start_hunt(Animals *animals, uint32_t slot, Combatant target)
{
    start_attack(&animals->list[slot], target);
    EntityId head = animals_leader(animals, animals_id_at(animals, slot));
    for (uint32_t other = 0; other < animals_slot_count(animals); other++) {
        EntityId id = animals_id_at(animals, other);
        Animal *mate = &animals->list[other];
        int dx = mate->x - animals->list[slot].x, dy = mate->y - animals->list[slot].y;
        if (other == slot || entity_id_is_none(id) || health_is_dead(&mate->health) ||
            health_is_down(&mate->health) || !entity_id_equal(animals_leader(animals, id), head) ||
            dx * dx + dy * dy > 4 * ALARM_RADIUS * ALARM_RADIUS) {
            continue;
        }
        start_attack(mate, target);
    }
}

/* A carnivore's way of eating: a carcass to go to, else a prey to hunt. */
static bool try_hunt(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    if (animal->search_cooldown[ANIMAL_SEARCH_FOOD] > 0) {
        return false;
    }
    Seeker seeker = {context, (AnimalSpecies)animal->species, false, def->flight_distance, false,
                     {0}};
    TilePos found;
    Combatant prey;
    if (context->items != NULL &&
        find_nearest(context, tile_of(animal), is_carcass, &seeker, ANIMAL_FOOD_SEARCH_MAX_COST,
                     &animal->path, &found)) {
        animal->search_failures[ANIMAL_SEARCH_FOOD] = 0;
        begin(animal, ANIMAL_TASK_EAT, food_for(&seeker, context->map, found, carcass_on));
        return true;
    }
    if (pick_prey(animals, slot, context, &prey)) {
        path_clear(&animal->path);
        animal->search_failures[ANIMAL_SEARCH_FOOD] = 0;
        start_hunt(animals, slot, prey);
        return true;
    }
    path_clear(&animal->path);
    search_failed(animal, ANIMAL_SEARCH_FOOD);
    return false;
}

static bool try_eat(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    return def->carnivore ? try_hunt(animals, slot, context) : try_forage(animal, def, context);
}

/* ---- Cold and heat -------------------------------------------------------- */

float animal_cold_limit(const Animal *animal)
{
    return ANIMAL_DEFS[animal->species].cold_below - attribute_cold_shift(&animal->attributes);
}

float animal_heat_limit(const Animal *animal)
{
    return ANIMAL_DEFS[animal->species].heat_above + attribute_heat_shift(&animal->attributes);
}

float animal_cold_pressure(const Animal *animal, float celsius)
{
    return clampf((animal_cold_limit(animal) - celsius) / ANIMAL_COLD_SPAN, 0.0f, 1.0f);
}

float animal_heat_pressure(const Animal *animal, float celsius)
{
    return clampf((celsius - animal_heat_limit(animal)) / ANIMAL_HEAT_SPAN, 0.0f, 1.0f);
}

float animal_walk_speed(const Animal *animal)
{
    return ANIMAL_DEFS[animal->species].walk_speed *
           attribute_factor(&animal->attributes, ATTRIBUTE_WALKING);
}

float animal_run_speed(const Animal *animal)
{
    return ANIMAL_DEFS[animal->species].flee_speed *
           attribute_factor(&animal->attributes, ATTRIBUTE_RUNNING);
}

float animal_sprint_seconds(const Animal *animal)
{
    return ANIMAL_DEFS[animal->species].sprint_seconds *
           attribute_factor(&animal->attributes, ATTRIBUTE_ENDURANCE) *
           fmaxf(health_vigor(&animal->health), 0.1f);
}

float animal_nerve(const Animal *animal)
{
    return ANIMAL_DEFS[animal->species].nerve * attribute_nerve(&animal->attributes) /
           ATTRIBUTE_NERVE_ORDINARY;
}

void animal_set_attributes(Animal *animal, const Attributes *attributes)
{
    animal->attributes = *attributes;
    health_set_factors(&animal->health, attribute_damage_factor(attributes),
                       attribute_pain_factor(attributes));
}

static float tile_celsius(const AnimalContext *context, int x, int y, int z)
{
    return context->temperatures != NULL ? temperature_at(context->temperatures, x, y, z)
                                         : ANIMAL_MILD_CELSIUS;
}

/* Whether a tile is a comfortable one for a species: not near freezing, nor
 * near overheating. */
static bool is_comfortable(const Animal *animal, float celsius)
{
    return celsius >= animal_cold_limit(animal) + ANIMAL_SHELTER_COLD_MARGIN &&
           celsius <= animal_heat_limit(animal) - ANIMAL_SHELTER_HEAT_MARGIN;
}

/* A search for a tile within a range of temperatures. */
typedef struct ShelterSeeker {
    Seeker seeker;
    const AnimalContext *context;
    float min, max;
} ShelterSeeker;

static bool is_shelter(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const ShelterSeeker *shelter = userdata;
    float celsius = tile_celsius(shelter->context, x, y, z);
    return celsius >= shelter->min && celsius <= shelter->max &&
           !near_seeker(&shelter->seeker, x, y, z);
}

/* Close to freezing or overheating where it stands, it goes to a tile that is
 * warmer (cooler) by a few degrees, if there is one within reach. */
static bool try_shelter(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    if (context->temperatures == NULL || animal->search_cooldown[ANIMAL_SEARCH_SHELTER] > 0) {
        return false;
    }
    float here = tile_celsius(context, animal->x, animal->y, animal->z);
    if (is_comfortable(animal, here)) {
        return false;
    }
    bool cold = here < animal_cold_limit(animal) + ANIMAL_SHELTER_COLD_MARGIN;
    ShelterSeeker shelter = {
        {context, (AnimalSpecies)animal->species, false, def->flight_distance, false, {0}},
        context,
        cold ? here + ANIMAL_SHELTER_GAIN : -1000.0f,
        cold ? 1000.0f : here - ANIMAL_SHELTER_GAIN};
    TilePos found;
    if (!find_nearest(context, tile_of(animal), is_shelter, &shelter, ANIMAL_SHELTER_MAX_COST,
                      &animal->path, &found)) {
        path_clear(&animal->path);
        search_failed(animal, ANIMAL_SEARCH_SHELTER);
        return false;
    }
    animal->search_failures[ANIMAL_SEARCH_SHELTER] = 0;
    begin(animal, ANIMAL_TASK_WANDER, found);
    return true;
}

static bool try_drink(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    if (animal->search_cooldown[ANIMAL_SEARCH_WATER] > 0) {
        return false;
    }
    Seeker seeker = {context, (AnimalSpecies)animal->species, false, def->flight_distance, false,
                     {0}};
    TilePos found;
    /* No water anywhere it could walk to: the search would cover everything
     * within its cost limit for nothing. */
    bool hopeless = context->regions != NULL &&
                    !regions_has_drinking_spot(context->regions, context->map,
                                               regions_at(context->regions, context->map, animal->x,
                                                          animal->y, animal->z));
    if (hopeless || !find_nearest(context, tile_of(animal), is_water, &seeker,
                                  ANIMAL_WATER_SEARCH_MAX_COST, &animal->path, &found)) {
        path_clear(&animal->path);
        search_failed(animal, ANIMAL_SEARCH_WATER);
        return false;
    }
    animal->search_failures[ANIMAL_SEARCH_WATER] = 0;
    begin(animal, ANIMAL_TASK_DRINK, found);
    return true;
}

/* The animal it follows, NULL for a leader and a loner. */
static const Animal *leader_of(const Animals *animals, const Animal *animal, uint32_t slot)
{
    EntityId leader = animal->leader;
    if (leader.index == slot || !pool_is_alive(&animals->pool, leader)) {
        return NULL;
    }
    return &animals->list[leader.index];
}

static bool far_from(const Animal *animal, const Animal *leader)
{
    int dx = leader->x - animal->x, dy = leader->y - animal->y, dz = leader->z - animal->z;
    return dx < -ANIMAL_HERD_RADIUS || dx > ANIMAL_HERD_RADIUS || dy < -ANIMAL_HERD_RADIUS ||
           dy > ANIMAL_HERD_RADIUS || dz < -1 || dz > 1;
}

/* Walks back to its herd, stopping a little short of its leader. */
static bool try_join(Animal *animal, const Animal *leader, const AnimalContext *context)
{
    if (!find_path(context, tile_of(animal), tile_of(leader), ANIMAL_HERD_MAX_COST,
                   &animal->path) ||
        animal->path.count <= HERD_STOP_SHORT) {
        path_clear(&animal->path);
        return false;
    }
    animal->path.count -= HERD_STOP_SHORT;
    while (animal->path.count > 0 &&
           !may_stop_at(context, animal->path.steps[animal->path.count - 1])) {
        animal->path.count--; /* Short of the others. */
    }
    if (animal->path.count == 0) {
        path_clear(&animal->path);
        return false;
    }
    begin(animal, ANIMAL_TASK_WANDER, animal->path.steps[animal->path.count - 1]);
    return true;
}

/* A stroll to a tile picked at random around `center`. */
static bool try_wander(Animal *animal, const AnimalDef *def, TilePos center,
                       const AnimalContext *context)
{
    const Map *map = context->map;
    int x = center.x + rng_range_i32(context->rng, -ANIMAL_WANDER_RADIUS, ANIMAL_WANDER_RADIUS);
    int y = center.y + rng_range_i32(context->rng, -ANIMAL_WANDER_RADIUS, ANIMAL_WANDER_RADIUS);
    int z = center.z;
    if (!path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z)) {
        z = map_ground_level(map, x, y); /* Up or down the relief. */
    }
    TilePos goal = {(int16_t)x, (int16_t)y, (int16_t)z};
    if ((x == animal->x && y == animal->y && z == animal->z) ||
        !path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z) || !may_stop_at(context, goal) ||
        near_threat(context, x, y, z, def->flight_distance) ||
        /* In a warm spot, it does not stroll off into the cold (nor the heat). */
        (is_comfortable(animal, tile_celsius(context, animal->x, animal->y, animal->z)) &&
         !is_comfortable(animal, tile_celsius(context, x, y, z))) ||
        !find_path(context, tile_of(animal), goal, ANIMAL_WANDER_MAX_COST, &animal->path)) {
        path_clear(&animal->path);
        return false;
    }
    begin(animal, ANIMAL_TASK_WANDER, goal);
    return true;
}

/* Goes for water or food when it has less than these, the most pressing
 * first. */
static bool try_needs(Animals *animals, uint32_t slot, const AnimalContext *context,
                      float food_below, float water_below)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    float food = animal->needs[ANIMAL_NEED_FOOD];
    float water = animal->needs[ANIMAL_NEED_WATER];
    bool hungry = food < food_below, thirsty = water < water_below;
    if (thirsty && (!hungry || water <= food)) {
        return try_drink(animal, def, context) || (hungry && try_eat(animals, slot, context));
    }
    if (hungry) {
        return try_eat(animals, slot, context) || (thirsty && try_drink(animal, def, context));
    }
    return false;
}

/* What to do next, standing on a tile center with nothing under way: what
 * presses first, then back to the herd, sleep when it is time, food and
 * water, and a stroll or nothing. */
static void decide(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    if (try_needs(animals, slot, context, ANIMAL_URGENT_NEED, ANIMAL_URGENT_NEED)) {
        return;
    }
    if (try_shelter(animal, def, context)) {
        return;
    }
    const Animal *leader = leader_of(animals, animal, slot);
    if (leader != NULL && far_from(animal, leader) && try_join(animal, leader, context)) {
        return; /* It eats and sleeps among the others. */
    }
    float rest = animal->needs[ANIMAL_NEED_REST];
    if (rest < ANIMAL_TIRED ||
        (rest < ANIMAL_REST_TIME_TIRED &&
         animal_rest_hour((AnimalSpecies)animal->species, game_hour_from_tick(context->tick)))) {
        begin(animal, ANIMAL_TASK_SLEEP, tile_of(animal));
        path_clear(&animal->path);
        return;
    }
    if (try_needs(animals, slot, context, ANIMAL_HUNGRY, ANIMAL_THIRSTY)) {
        return;
    }
    if (rng_chance(context->rng, 0.5f) &&
        try_wander(animal, def, leader != NULL ? tile_of(leader) : tile_of(animal), context)) {
        return;
    }
    stand(animal, random_idle_ticks(context->rng));
}

/* Eats what the tile offers: a bite. */
static void bite(Animal *animal, const AnimalDef *def, Map *map, TilePos at, AnimalFood food)
{
    size_t index = map_index(map, at.x, at.y, at.z);
    Plant *plant = &map->plants[index];
    float gain = 0.0f;
    switch (food) {
    case ANIMAL_FOOD_PLANT: {
        const PlantDef *plant_type = plant_def((PlantType)plant->type);
        /* A root dug up whole is eaten whole. */
        int taken = plant_take_stock(plant, PLANT_STOCK_FOOD,
                                     plant_type->dug_whole ? INT16_MAX : def->bite_items);
        gain = (float)taken * item_def(plant_type->food_item)->nutrition * def->food_value;
        if (plant_type->vanishes && !plant_has_food(plant)) {
            map_remove_plant(map, at.x, at.y, at.z); /* The patch is gone. */
        } else if (plant_type->dug_whole) {
            plant->growth = 0.0f; /* Grows again from what is left in the ground. */
            memset(plant->stock, 0, sizeof(plant->stock));
        }
        break;
    }
    case ANIMAL_FOOD_NUTS: {
        int taken = map_take_litter_count(map, at.x, at.y, at.z, ITEM_HAZELNUTS, def->bite_items);
        gain = (float)taken * item_def(ITEM_HAZELNUTS)->nutrition * def->food_value;
        break;
    }
    case ANIMAL_FOOD_GRASS: {
        float taken = fminf(def->graze_bite, plant->growth);
        plant->growth -= taken;
        gain = taken * def->graze_value;
        break;
    }
    case ANIMAL_FOOD_NONE: break;
    }
    animal->needs[ANIMAL_NEED_FOOD] = clampf(animal->needs[ANIMAL_NEED_FOOD] + gain, 0.0f, 1.0f);
}

static void eat_plants(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    Map *map = context->map;
    TilePos food = animal->target;
    bool there = within_bite(map, tile_of(animal), food) &&
                 animal_food_at((AnimalSpecies)animal->species, map, food.x, food.y, food.z) !=
                     ANIMAL_FOOD_NONE;
    if (!there) {
        /* The way was cut, or the food is gone (eaten, picked): elsewhere. */
        if (!try_forage(animal, def, context)) {
            stand(animal, random_idle_ticks(context->rng));
        }
        return;
    }
    if (++animal->timer < ANIMAL_BITE_TICKS) {
        return;
    }
    animal->timer = 0;
    bite(animal, def, map, food,
         animal_food_at((AnimalSpecies)animal->species, map, food.x, food.y, food.z));
    if (animal->needs[ANIMAL_NEED_FOOD] >= ANIMAL_SATED) {
        stand(animal, random_idle_ticks(context->rng));
    }
}

/* A carnivore at a carcass: a bite now and then, until it is full or the
 * carcass is gone. */
static void eat_carcass(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    Items *items = context->items;
    TilePos food = animal->target;
    ItemStack stack =
        items != NULL ? items_get(items, food.x, food.y, food.z) : (ItemStack){.type = ITEM_NONE};
    bool there =
        within_bite(context->map, tile_of(animal), food) && carcass_edible((ItemType)stack.type);
    if (!there) {
        /* The way was cut, or the carcass is gone (eaten, rotted): elsewhere. */
        if (!try_hunt(animals, slot, context)) {
            stand(animal, random_idle_ticks(context->rng));
        }
        return;
    }
    if (++animal->timer < ANIMAL_CARCASS_BITE_TICKS) {
        return;
    }
    animal->timer = 0;
    /* The bite takes weight off the carcass, not hit points (rot takes those);
     * what it fills is what was left to take. */
    ItemStack one = stack;
    one.count = 1;
    float eaten_kg = item_stack_eat(&one, def->bite_kg);
    items_remove(items, food.x, food.y, food.z, 1);
    if (one.type != ITEM_NONE) {
        items_add(items, food.x, food.y, food.z, one);
    }
    animal->needs[ANIMAL_NEED_FOOD] =
        clampf(animal->needs[ANIMAL_NEED_FOOD] + eaten_kg / def->stomach_kg, 0.0f, 1.0f);
    if (animal->needs[ANIMAL_NEED_FOOD] >= ANIMAL_SATED) {
        stand(animal, random_idle_ticks(context->rng));
    }
}

static void drink(Animal *animal, const AnimalContext *context)
{
    if (animal->x != animal->target.x || animal->y != animal->target.y ||
        animal->z != animal->target.z ||
        !map_is_drinking_spot(context->map, animal->x, animal->y, animal->z)) {
        stand(animal, 0); /* The way was cut, or the water is gone: look again. */
        return;
    }
    if (++animal->timer < ANIMAL_DRINK_TICKS) {
        return;
    }
    animal->needs[ANIMAL_NEED_WATER] = 1.0f;
    stand(animal, random_idle_ticks(context->rng));
}

static void sleep_on(Animal *animal, const AnimalContext *context)
{
    float rest = animal->needs[ANIMAL_NEED_REST];
    bool resting_hours =
        animal_rest_hour((AnimalSpecies)animal->species, game_hour_from_tick(context->tick));
    /* Hunger and thirst wake it up, when there is hope of finding something. */
    bool pressed = (animal->needs[ANIMAL_NEED_FOOD] < ANIMAL_URGENT_NEED &&
                    animal->search_cooldown[ANIMAL_SEARCH_FOOD] == 0) ||
                   (animal->needs[ANIMAL_NEED_WATER] < ANIMAL_URGENT_NEED &&
                    animal->search_cooldown[ANIMAL_SEARCH_WATER] == 0);
    /* Cold or heat wake it too: it looks for a better spot. */
    pressed |= (animal->health.deprivation[DEPRIVATION_COLD] > 0.05f ||
                animal->health.deprivation[DEPRIVATION_HEAT] > 0.05f) &&
               animal->search_cooldown[ANIMAL_SEARCH_SHELTER] == 0;
    if (rest >= 1.0f || (!resting_hours && rest >= ANIMAL_RESTED) || pressed) {
        stand(animal, 0);
    }
}

/* Moves one tick along the path. */
static void walk(Animal *animal, const AnimalContext *context)
{
    const Map *map = context->map;
    TilePos next = animal->path.steps[animal->path_index];
    bool open = animal->move_progress > 0.0f
                    ? path_walker_can_stand(PATH_WALKER_ANIMAL, map, next.x, next.y, next.z)
                    : path_walker_can_step(PATH_WALKER_ANIMAL, map, tile_of(animal), next);
    if (!open) {
        animal->move_progress = 0.0f;
        stand(animal, random_idle_ticks(context->rng)); /* The way was cut. */
        return;
    }
    Occupancy *occupancy = context->occupancy;
    if (occupancy != NULL && animal->move_progress == 0.0f &&
        animal->path_index == animal->path.count - 1 &&
        !occupancy_holds(occupancy, context->self, next)) {
        return; /* Stops only where it may: settle() sorts it out. */
    }
    float cost = map_move_cost(map, next.x, next.y, next.z);
    if (next.z != animal->z) {
        cost *= MAP_CLIMB_COST;
    } else if (next.x != animal->x && next.y != animal->y) {
        cost *= SQRT2;
    }
    if (next.x != animal->x) {
        animal->facing_left = next.x < animal->x;
    }
    bool running = animal->task == ANIMAL_TASK_FLEE || animal->task == ANIMAL_TASK_ATTACK;
    float speed =
        (running ? animal_run_speed(animal) * (1.0f - ANIMAL_FATIGUE_SLOWDOWN * animal->fatigue)
                 : animal_walk_speed(animal)) *
        health_ability(&animal->health, HEALTH_WALKING);
    if (occupancy != NULL && occupancy_others_on(occupancy, context->self, next) > 0) {
        speed *= OCCUPANCY_CROSSING_SPEED; /* Squeezes past someone. */
    }
    animal->move_progress += speed / (float)TICKS_PER_SECOND / cost;
    if (animal->move_progress >= 1.0f) {
        animal->x = next.x;
        animal->y = next.y;
        animal->z = next.z;
        animal->move_progress = 0.0f;
        animal->path_index++;
        if (animal->path_index >= animal->path.count) {
            animal->path_index = 0;
            path_clear(&animal->path);
        }
    }
}

/* ---- Fear ----------------------------------------------------------------- */

/* Runs off to the nearest tile far enough from every settler. False when it
 * stays where it is: far enough already, or cornered. */
static bool flee(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    Seeker seeker = {context,
                     (AnimalSpecies)animal->species,
                     false,
                     def->flight_distance + ANIMAL_FLEE_MARGIN,
                     !combatant_is_none(animal->enemy),
                     animal->enemy_pos};
    TilePos found;
    if (animal->flee_cooldown > 0) {
        return false;
    }
    if (!find_nearest(context, tile_of(animal), is_refuge, &seeker, ANIMAL_FLEE_MAX_COST,
                      &animal->path, &found)) {
        animal->flee_cooldown = ANIMAL_CORNERED_TICKS; /* Cornered. */
        stand(animal, WATCH_TICKS);
        return false;
    }
    if (animal->path.count == 0) {
        stand(animal, WATCH_TICKS); /* Far enough already: on the watch. */
        return false;
    }
    begin(animal, ANIMAL_TASK_FLEE, found);
    return true;
}

/* Looks around: runs off from a settler it sees (its herd with it), or from
 * where a herd mate saw one. */
static void look_out(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    if (animal->alarmed) {
        animal->alarmed = false;
        flee(animal, def, context);
        return;
    }
    /* Whoever struck it is still about: it keeps running. */
    bool seen = false;
    bool fearless = def->hunts_settlers && animal->needs[ANIMAL_NEED_FOOD] < ANIMAL_DESPERATE;
    float alertness = health_vigor(&animal->health);
    if (!combatant_is_none(animal->enemy)) {
        int dx = animal->enemy_pos.x - animal->x, dy = animal->enemy_pos.y - animal->y;
        seen = dx * dx + dy * dy <= def->flight_distance * def->flight_distance;
    }
    for (int i = 0; i < context->threat_count && !seen && !fearless; i++) {
        seen = notices_threat((AnimalSpecies)animal->species, context->map, animal->x, animal->y,
                              animal->z, context->threats[i], animal->task == ANIMAL_TASK_SLEEP,
                              alertness);
    }
    /* Prey fear the carnivores about. */
    if (!seen && !def->carnivore && combatant_is_none(animal->enemy)) {
        for (int i = 0; i < animals->predator_count && !seen; i++) {
            const Animal *predator = &animals->list[animals->predator_slots[i]];
            if (notices_threat((AnimalSpecies)animal->species, context->map, animal->x, animal->y,
                               animal->z, tile_of(predator), animal->task == ANIMAL_TASK_SLEEP,
                               alertness)) {
                animal->enemy = (Combatant){COMBATANT_ANIMAL,
                                            animals_id_at(animals, animals->predator_slots[i])};
                animal->enemy_pos = tile_of(predator);
                seen = true;
            }
        }
    }
    if (!seen || !flee(animal, def, context)) {
        return;
    }
    /* Its herd mates close by run off too, next time they look around. */
    EntityId head = animals_leader(animals, animals_id_at(animals, slot));
    for (uint32_t other = 0; other < animals_slot_count(animals); other++) {
        EntityId id = animals_id_at(animals, other);
        Animal *mate = &animals->list[other];
        if (other == slot || entity_id_is_none(id) || mate->task == ANIMAL_TASK_FLEE ||
            !entity_id_equal(animals_leader(animals, id), head)) {
            continue;
        }
        int dx = mate->x - animal->x, dy = mate->y - animal->y;
        if (dx * dx + dy * dy <= ALARM_RADIUS * ALARM_RADIUS) {
            mate->alarmed = true;
            if (combatant_is_none(mate->enemy) && !combatant_is_none(animal->enemy)) {
                mate->enemy = animal->enemy; /* From the same predator. */
                mate->enemy_pos = animal->enemy_pos;
            }
        }
    }
}

/* ---- Combat --------------------------------------------------------------- */

/* Where a combatant is now; false when it is gone: dead, not there any more. */
static bool enemy_tile(const Animals *animals, const AnimalContext *context, Combatant who,
                       TilePos *out)
{
    if (who.kind == COMBATANT_ANIMAL) {
        const Animal *other = animals_get_const(animals, who.id);
        if (other == NULL || health_is_dead(&other->health)) {
            return false;
        }
        *out = tile_of(other);
        return true;
    }
    for (int i = 0; who.kind == COMBATANT_SETTLER && i < context->settler_count; i++) {
        if (entity_id_equal(context->settlers[i].id, who.id)) {
            *out = context->settlers[i].pos;
            return true;
        }
    }
    return false;
}

/* Keeps track of its enemy: where it is now, or forgets it when it is gone or
 * too far. */
static void track_enemy(const Animals *animals, Animal *animal, const AnimalContext *context)
{
    if (combatant_is_none(animal->enemy)) {
        return;
    }
    TilePos at;
    if (!enemy_tile(animals, context, animal->enemy, &at) ||
        abs(at.x - animal->x) > ANIMAL_ENEMY_FORGET_DISTANCE ||
        abs(at.y - animal->y) > ANIMAL_ENEMY_FORGET_DISTANCE ||
        abs(at.z - animal->z) > ANIMAL_ALERT_LEVELS) {
        animal->enemy = combatant_none();
        if (animal->task == ANIMAL_TASK_ATTACK) {
            stand(animal, WATCH_TICKS);
        }
        return;
    }
    animal->enemy_pos = at;
}

/* On a tile center, attacking: breaks off when hurt past its nerve, strikes
 * when its enemy is in reach and it is ready, else walks toward it. */
static void fight(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    if (combatant_is_none(animal->enemy) || combat_choice_count(def->attacks) == 0) {
        stand(animal, WATCH_TICKS);
        return;
    }
    /* Past its nerve, or with nothing left to strike with (every limb that
     * carries an attack is ruined, or it is knocked out): it runs from it. */
    if (combat_breaks_off(&animal->health, animal_nerve(animal)) ||
        !combat_can_attack(&animal->health, def->attacks)) {
        animal->alarmed = true;
        stand(animal, 0);
        return;
    }
    TilePos here = tile_of(animal);
    int reach = combat_choice_reach(&animal->health, def->attacks);
    if (combat_can_strike(context->map, here, animal->enemy_pos, reach)) {
        if (animal->attack_cooldown <= 0 && context->combat != NULL) {
            float power = 1.0f;
            AttackType attack =
                combat_choose_attack(&animal->health, def->attacks, context->rng, &power);
            combat_queue_push(context->combat,
                              (CombatStrike){{COMBATANT_ANIMAL, animals_id_at(animals, slot)},
                                             animal->enemy,
                                             (uint8_t)attack,
                                             1.0f,
                                             power});
            animal->attack_cooldown = attack_interval_ticks(attack);
        }
        if (may_stop_at(context, here)) {
            animal->path_index = 0;
            path_clear(&animal->path);
            animal->timer = 0;
            animal->surround_waits = 0;
            return;
        }
        /* In reach on its way, but someone else stops here: it strikes in
         * passing and goes on to a free tile around its enemy. */
    } else if (!animal_is_moving(animal) && animal->timer > 0) {
        animal->timer--; /* Waits for room around it. */
        return;
    }
    animal->path_index = 0;
    /* A tile beside it, free (others already around it keep theirs). */
    CombatSpot spot = {animal->enemy_pos, reach};
    TileBox box = combat_spot_box(&spot);
    TilePos found;
    context->pathfinder->walker = PATH_WALKER_ANIMAL;
    bool chase = pathfinder_find_nearest_in_box(context->pathfinder, context->map, here, &box,
                                                combat_is_strike_spot, &spot, ANIMAL_CHASE_MAX_COST,
                                                &animal->path, &found);
    context->pathfinder->walker = PATH_WALKER_SETTLER;
    if (chase) {
        animal->surround_waits = 0;
        return;
    }
    if (find_path(context, here, animal->enemy_pos, ANIMAL_CHASE_MAX_COST, &animal->path) &&
        animal->surround_waits < ANIMAL_SURROUND_PATIENCE) {
        path_clear(&animal->path); /* No room around it: it waits a little. */
        animal->surround_waits++;
        animal->timer = COMBAT_SURROUND_RETRY_TICKS;
        return;
    }
    /* No way to it, or no room for long: it lets go (and may hunt another). */
    animal->enemy = combatant_none();
    animal->surround_waits = 0;
    animal->search_cooldown[ANIMAL_SEARCH_FOOD] = ANIMAL_SEARCH_RETRY_TICKS;
    stand(animal, WATCH_TICKS);
}

/* Whatever it was doing is given up (without a jolt while it is between two
 * tiles: it finishes the step). */
static void interrupt(Animal *animal)
{
    if (animal->move_progress > 0.0f) {
        animal->task = ANIMAL_TASK_IDLE;
        animal->timer = 0;
    } else {
        stand(animal, 0);
    }
}

static void start_attack(Animal *animal, Combatant enemy)
{
    animal->enemy = enemy;
    animal->attack_cooldown = 0;
    animal->alarmed = false;
    interrupt(animal);
    animal->task = ANIMAL_TASK_ATTACK;
}

bool animals_attack(Animals *animals, EntityId id, Combatant enemy)
{
    Animal *animal = animals_get(animals, id);
    if (animal == NULL || health_is_dead(&animal->health) ||
        combat_choice_count(ANIMAL_DEFS[animal->species].attacks) == 0) {
        return false;
    }
    if (enemy.kind == COMBATANT_ANIMAL) {
        const Animal *other = animals_get_const(animals, enemy.id);
        if (other == NULL || health_is_dead(&other->health) || entity_id_equal(enemy.id, id)) {
            return false;
        }
        animal->enemy_pos = tile_of(other);
    } else if (enemy.kind != COMBATANT_SETTLER) {
        return false;
    }
    start_attack(animal, enemy);
    return true;
}

bool animals_provoke(Animals *animals, EntityId id, Combatant attacker)
{
    Animal *animal = animals_get(animals, id);
    if (animal == NULL || health_is_dead(&animal->health) || combatant_is_none(attacker)) {
        return false;
    }
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    bool fresh = combatant_is_none(animal->enemy);
    if (fresh) {
        animal->enemy = attacker;
        if (attacker.kind == COMBATANT_ANIMAL) {
            const Animal *other = animals_get_const(animals, attacker.id);
            if (other != NULL) {
                animal->enemy_pos = tile_of(other);
            }
        }
    }
    if (health_is_down(&animal->health)) {
        return fresh;
    }
    if (def->fights_back && combat_can_attack(&animal->health, def->attacks) &&
        !combat_breaks_off(&animal->health, animal_nerve(animal))) {
        if (animal->task != ANIMAL_TASK_ATTACK) {
            start_attack(animal, animal->enemy);
        }
    } else if (animal->task != ANIMAL_TASK_FLEE) {
        animal->alarmed = true;
        animal->alert_wait = 0;
        interrupt(animal);
    }
    return fresh;
}

/* ---- Tick ----------------------------------------------------------------- */

static void update_needs(Animal *animal, const AnimalDef *def)
{
    const float hour = (float)TICKS_PER_HOUR;
    float *needs = animal->needs;
    needs[ANIMAL_NEED_FOOD] =
        clampf(needs[ANIMAL_NEED_FOOD] - attribute_factor(&animal->attributes, ATTRIBUTE_APPETITE) /
                                             (def->hunger_hours * hour),
               0.0f, 1.0f);
    needs[ANIMAL_NEED_WATER] =
        clampf(needs[ANIMAL_NEED_WATER] - 1.0f / (def->thirst_hours * hour), 0.0f, 1.0f);
    float rest =
        animal->task == ANIMAL_TASK_SLEEP
            ? attribute_factor(&animal->attributes, ATTRIBUTE_SLEEP) / (def->sleep_hours * hour)
            : -1.0f / (def->awake_hours * hour);
    needs[ANIMAL_NEED_REST] = clampf(needs[ANIMAL_NEED_REST] + rest, 0.0f, 1.0f);
}

static void tick_one(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    if (health_is_dead(&animal->health)) {
        return; /* The game leaves its carcass (core/game.h). */
    }
    if (health_is_hurt(&animal->health) &&
        health_update(&animal->health, 1.0f / (float)TICKS_PER_HOUR)) {
        return; /* Died. */
    }
    update_needs(animal, def);
    if ((context->tick + (uint64_t)slot) % ANIMAL_STRAIN_TICKS == 0) {
        /* Nothing to eat or drink for long, or the cold or the heat: it wastes away. */
        float hours = (float)ANIMAL_STRAIN_TICKS / (float)TICKS_PER_HOUR;
        bool starved = health_strain(&animal->health, DEPRIVATION_HUNGER,
                                     animal->needs[ANIMAL_NEED_FOOD] <= 0.0f ? 1.0f : 0.0f, hours);
        starved |= health_strain(&animal->health, DEPRIVATION_THIRST,
                                 animal->needs[ANIMAL_NEED_WATER] <= 0.0f ? 1.0f : 0.0f, hours);
        float celsius = tile_celsius(context, animal->x, animal->y, animal->z);
        starved |= health_strain(&animal->health, DEPRIVATION_COLD,
                                 animal_cold_pressure(animal, celsius), hours);
        starved |= health_strain(&animal->health, DEPRIVATION_HEAT,
                                 animal_heat_pressure(animal, celsius), hours);
        if (starved) {
            return;
        }
    }
    if (health_is_down(&animal->health)) {
        if (animal->task != ANIMAL_TASK_DOWN) {
            animal->task = ANIMAL_TASK_DOWN; /* Falls where it is. */
            animal->move_progress = 0.0f;
            animal->path_index = 0;
            path_clear(&animal->path);
            animal->alarmed = false;
        }
        return;
    }
    if (animal->task == ANIMAL_TASK_DOWN) {
        stand(animal, 0); /* Gets up. */
    }
    for (int search = 0; search < ANIMAL_SEARCH_COUNT; search++) {
        if (animal->search_cooldown[search] > 0) {
            animal->search_cooldown[search]--;
        }
    }
    if (animal->flee_cooldown > 0) {
        animal->flee_cooldown--;
    }
    if (animal->alert_wait > 0) {
        animal->alert_wait--;
    }
    if (animal->attack_cooldown > 0) {
        animal->attack_cooldown--;
    }
    track_enemy(animals, animal, context);
    /* Running wears it out; resting restores it. */
    bool sprinting = (animal->task == ANIMAL_TASK_FLEE || animal->task == ANIMAL_TASK_ATTACK) &&
                     animal_is_moving(animal);
    float endurance =
        attribute_factor(&animal->attributes, ATTRIBUTE_ENDURANCE) * health_vigor(&animal->health);
    float tire = sprinting ? 1.0f / (animal_sprint_seconds(animal) * (float)TICKS_PER_SECOND)
                           : -endurance / (ANIMAL_RECOVER_SECONDS * (float)TICKS_PER_SECOND);
    animal->fatigue = clampf(animal->fatigue + tire, 0.0f, 1.0f);
    /* On a tile center, and not running or fighting already: a look around now
     * and then. */
    bool running = animal->task == ANIMAL_TASK_FLEE && animal_is_moving(animal);
    if (animal->move_progress <= 0.0f && !running && animal->task != ANIMAL_TASK_ATTACK &&
        (animal->alarmed || animal->alert_wait == 0)) {
        animal->alert_wait = ANIMAL_ALERT_TICKS;
        look_out(animals, slot, context);
    }
    if (animal->task == ANIMAL_TASK_ATTACK && animal->move_progress <= 0.0f) {
        fight(animals, slot, context);
    }
    if (animal_is_moving(animal)) {
        walk(animal, context);
        return;
    }
    switch ((AnimalTask)animal->task) {
    case ANIMAL_TASK_IDLE:
        if (--animal->timer <= 0) {
            decide(animals, slot, context);
        }
        break;
    case ANIMAL_TASK_WANDER: stand(animal, random_idle_ticks(context->rng)); break;
    case ANIMAL_TASK_FLEE: stand(animal, WATCH_TICKS); break; /* Out of breath, on the watch. */
    case ANIMAL_TASK_EAT:
        if (def->carnivore) {
            eat_carcass(animals, slot, context);
        } else {
            eat_plants(animal, def, context);
        }
        break;
    case ANIMAL_TASK_DRINK: drink(animal, context); break;
    case ANIMAL_TASK_SLEEP: sleep_on(animal, context); break;
    case ANIMAL_TASK_ATTACK:
    case ANIMAL_TASK_DOWN:
    case ANIMAL_TASK_COUNT: break;
    }
}

/* ---- Where it stops (core/occupancy.h) ------------------------------------- */

static bool is_free_spot(const Map *map, int x, int y, int z, void *userdata)
{
    const AnimalContext *context = userdata;
    return path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z) &&
           may_stop_at(context, (TilePos){(int16_t)x, (int16_t)y, (int16_t)z});
}

/* Somewhere else to stand: the nearest free tile (a fight or a flight goes
 * on from there, anything else is given up). */
static void step_aside(Animal *animal, const AnimalContext *context)
{
    TilePos spot;
    if (!find_nearest(context, tile_of(animal), is_free_spot, (void *)context,
                      ANIMAL_MAKE_WAY_MAX_COST, &animal->path, &spot) ||
        animal->path.count == 0) {
        path_clear(&animal->path);
        return; /* Nowhere: it tries again on its next turn. */
    }
    animal->path_index = 0;
    if (animal->task == ANIMAL_TASK_ATTACK || animal->task == ANIMAL_TASK_FLEE) {
        occupancy_claim(context->occupancy, context->self, spot); /* Still at it. */
        return;
    }
    begin(animal, ANIMAL_TASK_WANDER, spot);
    occupancy_set_yielding(context->occupancy, context->self, true);
    occupancy_claim(context->occupancy, context->self, spot);
}

/* The tile it was to stop on is someone else's, and it waited for it long
 * enough: a fight or a flight goes on from where it stands, anything else is
 * given up. Standing on someone else's tile, it steps aside. */
static void make_way(Animal *animal, const AnimalContext *context)
{
    animal->stop_wait = 0;
    if (animal_is_moving(animal)) {
        if (animal->task == ANIMAL_TASK_ATTACK || animal->task == ANIMAL_TASK_FLEE) {
            animal->path_index = 0;
            path_clear(&animal->path);
        } else {
            stand(animal, 0);
            occupancy_set_yielding(context->occupancy, context->self, true);
        }
    }
    if (!occupancy_claim(context->occupancy, context->self, tile_of(animal))) {
        step_aside(animal, context);
    }
}

/* Lying on a tile where someone else lies already: it crawls to the nearest
 * free tile close by, and lies there. */
static void crawl_aside(Animal *animal, const AnimalContext *context)
{
    TilePos here = tile_of(animal), spot;
    bool found = find_nearest(context, here, is_free_spot, (void *)context,
                              ANIMAL_MAKE_WAY_MAX_COST, &animal->path, &spot);
    path_clear(&animal->path);
    animal->path_index = 0;
    if (!found || spot.z != here.z || abs(spot.x - here.x) > OCCUPANCY_CRAWL_REACH ||
        abs(spot.y - here.y) > OCCUPANCY_CRAWL_REACH) {
        return; /* Nowhere close: it lies on top of the other. */
    }
    animal->x = spot.x;
    animal->y = spot.y;
    animal->move_progress = 0.0f;
    occupancy_place(context->occupancy, context->self, spot);
    occupancy_force(context->occupancy, context->self, spot);
}

/* After its turn, as for the settlers (see settle() in core/colonist.c). */
static void settle(Animal *animal, const AnimalContext *context)
{
    Occupancy *occupancy = context->occupancy;
    BeingId self = context->self;
    if (health_is_dead(&animal->health)) {
        occupancy_remove(occupancy, self);
        return;
    }
    TilePos here = tile_of(animal);
    occupancy_place(occupancy, self, here);
    if (health_is_down(&animal->health)) {
        occupancy_set_yielding(occupancy, self, false);
        if (!occupancy_force(occupancy, self, here)) { /* It cannot move: others make way. */
            crawl_aside(animal, context);              /* ...unless they lie there too. */
        }
        return;
    }
    occupancy_set_yielding(occupancy, self,
                           animal->task == ANIMAL_TASK_IDLE || animal->task == ANIMAL_TASK_WANDER);
    bool moving = animal_is_moving(animal);
    TilePos stop = moving ? animal->path.steps[animal->path.count - 1] : here;
    if (occupancy_holds(occupancy, self, stop) || occupancy_claim(occupancy, self, stop)) {
        animal->stop_wait = 0;
        return;
    }
    occupancy_release(occupancy, self); /* Not where it was any more. */
    if (animal->move_progress > 0.0f) {
        return; /* Between two tiles: it sees on the next one. */
    }
    bool last_step = moving && animal->path_index >= animal->path.count - 1;
    if (moving && !last_step) {
        return; /* Still on its way: the tile may be free when it gets there. */
    }
    if (last_step && animal->task != ANIMAL_TASK_WANDER &&
        ++animal->stop_wait < OCCUPANCY_PATIENCE_TICKS) {
        return; /* Waits for it a little. */
    }
    make_way(animal, context);
}

void animals_tick(Animals *animals, const AnimalContext *context)
{
    animals->predator_count = 0;
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        const Animal *animal = &animals->list[slot];
        if (!entity_id_is_none(animals_id_at(animals, slot)) &&
            ANIMAL_DEFS[animal->species].carnivore && !health_is_dead(&animal->health) &&
            !health_is_down(&animal->health)) {
            animals->predator_slots[animals->predator_count++] = slot;
        }
    }
    /* Fixed slot order keeps the simulation deterministic. */
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        if (entity_id_is_none(animals_id_at(animals, slot))) {
            continue;
        }
        if (context->occupancy == NULL) {
            tick_one(animals, slot, context);
            continue;
        }
        /* Its searches skip the tiles others hold. */
        AnimalContext turn = *context;
        turn.self = occupancy_animal(slot);
        OccupancyStopper stopper = {context->occupancy, turn.self};
        context->pathfinder->stop_filter = occupancy_stop_filter;
        context->pathfinder->stop_userdata = &stopper;
        tick_one(animals, slot, &turn);
        settle(&animals->list[slot], &turn);
        context->pathfinder->stop_filter = NULL;
        context->pathfinder->stop_userdata = NULL;
    }
}

/* ---- Populating a world --------------------------------------------------- */

/* Dry ground an animal can stand on. */
static bool is_land(const Map *map, int x, int y, int z)
{
    return path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z) && !map_is_water(map, x, y, z) &&
           !terrain_is_foliage(map->terrain[map_index(map, x, y, z)]);
}

static bool is_free_land(const Map *map, int x, int y, int z, void *userdata)
{
    return is_land(map, x, y, z) && !animals_on_tile(userdata, x, y, z);
}

int animals_populate(Animals *animals, const Map *map, Pathfinder *pathfinder,
                     const Regions *regions, Rng *rng, float density, const TilePos *avoid)
{
    if (density <= 0.0f) {
        return 0;
    }
    size_t land = 0;
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            land += is_land(map, x, y, map_ground_level(map, x, y)) ? 1 : 0;
        }
    }
    int created = 0;
    for (int species = 0; species < ANIMAL_SPECIES_COUNT; species++) {
        const AnimalDef *def = &ANIMAL_DEFS[species];
        int wanted = (int)(def->density * density * (float)land / 10000.0f + 0.5f);
        int attempts = wanted * POPULATE_ATTEMPTS;
        /* Ground that leads to water first; anywhere when there is none. */
        int thirsty_attempts = attempts / 3;
        while (wanted > 0 && attempts-- > 0 && animals->pool.count < ANIMAL_MAX) {
            int x = (int)rng_range_u32(rng, (uint32_t)map->width);
            int y = (int)rng_range_u32(rng, (uint32_t)map->height);
            int z = map_ground_level(map, x, y);
            if (!is_land(map, x, y, z)) {
                continue;
            }
            if (avoid != NULL) {
                int dx = avoid->x - x, dy = avoid->y - y;
                if (dx * dx + dy * dy < ANIMAL_SPAWN_CLEARANCE * ANIMAL_SPAWN_CLEARANCE) {
                    continue;
                }
            }
            if (attempts >= thirsty_attempts && regions != NULL &&
                !regions_has_drinking_spot(regions, map, regions_at(regions, map, x, y, z))) {
                continue;
            }
            bool male = rng_chance(rng, def->male_share);
            int size = male ? rng_range_i32(rng, 1, def->male_group_max)
                            : rng_range_i32(rng, def->herd_min, def->herd_max);
            EntityId leader = ENTITY_NONE;
            for (int i = 0; i < size && wanted > 0; i++) {
                TilePos spot = {(int16_t)x, (int16_t)y, (int16_t)z};
                TilePos tile;
                pathfinder->walker = PATH_WALKER_ANIMAL;
                bool found = pathfinder_find_nearest(pathfinder, map, spot, is_free_land, animals,
                                                     POPULATE_SPREAD_COST, NULL, &tile);
                pathfinder->walker = PATH_WALKER_SETTLER;
                if (!found) {
                    break;
                }
                EntityId id =
                    animals_spawn(animals, map, (AnimalSpecies)species,
                                  male ? ANIMAL_MALE : ANIMAL_FEMALE, tile.x, tile.y, tile.z, rng);
                if (entity_id_is_none(id)) {
                    break;
                }
                if (entity_id_is_none(leader)) {
                    leader = id;
                } else {
                    animals_set_leader(animals, id, leader);
                }
                created++;
                wanted--;
            }
        }
    }
    return created;
}

/* ---- Hash ----------------------------------------------------------------- */

uint64_t animals_hash(const Animals *animals, uint64_t hash)
{
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        EntityId id = animals_id_at(animals, slot);
        if (entity_id_is_none(id)) {
            continue;
        }
        const Animal *animal = &animals->list[slot];
        int32_t fields[22] = {(int32_t)id.index,
                              (int32_t)id.generation,
                              animal->species,
                              animal->sex,
                              animal->x,
                              animal->y,
                              animal->z,
                              animal->task,
                              animal->target.x,
                              animal->target.y,
                              animal->target.z,
                              animal->timer,
                              (int32_t)animal->leader.index,
                              (int32_t)animal->leader.generation,
                              animal->alarmed,
                              animal->alert_wait,
                              animal->flee_cooldown,
                              animal->path_index,
                              animal->search_cooldown[ANIMAL_SEARCH_FOOD],
                              animal->search_cooldown[ANIMAL_SEARCH_WATER],
                              animal->search_failures[ANIMAL_SEARCH_FOOD],
                              animal->search_failures[ANIMAL_SEARCH_WATER]};
        hash = hash_fnv1a64(hash, fields, sizeof(fields));
        hash = hash_fnv1a64(hash, &animal->move_progress, sizeof(animal->move_progress));
        hash = hash_fnv1a64(hash, animal->needs, sizeof(animal->needs));
        hash = health_hash(&animal->health, hash);
        hash = attributes_hash(&animal->attributes, hash);
        if (animal->fatigue > 0.0f) { /* Likewise. */
            hash = hash_fnv1a64(hash, &animal->fatigue, sizeof(animal->fatigue));
        }
        if (!combatant_is_none(animal->enemy) || animal->attack_cooldown > 0) {
            /* Only then: hashes of games without fights stay as they were. */
            int32_t fight[8] = {animal->enemy.kind,
                                (int32_t)animal->enemy.id.index,
                                (int32_t)animal->enemy.id.generation,
                                animal->enemy_pos.x,
                                animal->enemy_pos.y,
                                animal->enemy_pos.z,
                                animal->attack_cooldown,
                                0};
            hash = hash_fnv1a64(hash, fight, sizeof(fight));
        }
    }
    return hash;
}
