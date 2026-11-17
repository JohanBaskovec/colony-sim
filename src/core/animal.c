#include "core/animal.h"

#include "core/i18n.h"
#include "core/item.h"
#include "core/util.h"

#include <math.h>
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
                         .food_value = 1.5f},
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
                         .food_value = 0.5f},
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
         .food_value = 0.8f},
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
         .food_value = 6.0f},
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
    animal->leader = id;
    animal->alert_wait = (int)(id.index % ANIMAL_ALERT_TICKS); /* Not all on the same tick. */
    animal->facing_left = rng_chance(rng, 0.5f);
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

bool animal_notices(AnimalSpecies species, const Map *map, int x, int y, int z, TilePos threat,
                    bool asleep)
{
    const AnimalDef *def = animal_def(species);
    if (def == NULL) {
        return false;
    }
    int distance = asleep ? def->flight_distance / 2 : def->flight_distance;
    int scent = distance / ANIMAL_SCENT_SHARE;
    int dx = threat.x - x, dy = threat.y - y, dz = threat.z - z;
    if (dz >= -1 && dz <= 1 && dx * dx + dy * dy <= scent * scent) {
        return true; /* Heard or smelled. */
    }
    return animal_sees(map, x, y, z, threat, distance);
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

/* What a search looks for, and for whom. */
typedef struct Seeker {
    const AnimalContext *context;
    AnimalSpecies species;
    bool treats_only; /* Food: nothing less than fruit, roots or nuts. */
    int clearance;    /* Tiles to keep from every settler. */
} Seeker;

static bool is_food(const Map *map, int x, int y, int z, void *userdata)
{
    const Seeker *seeker = userdata;
    AnimalFood food = animal_food_at(seeker->species, map, x, y, z);
    return (seeker->treats_only ? food > ANIMAL_FOOD_GRASS : food != ANIMAL_FOOD_NONE) &&
           !near_threat(seeker->context, x, y, z, seeker->clearance);
}

static bool is_water(const Map *map, int x, int y, int z, void *userdata)
{
    const Seeker *seeker = userdata;
    return map_is_drinking_spot(map, x, y, z) &&
           !near_threat(seeker->context, x, y, z, seeker->clearance);
}

static bool is_refuge(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const Seeker *seeker = userdata;
    return !near_threat(seeker->context, x, y, z, seeker->clearance);
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

static bool try_eat(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    if (animal->search_cooldown[ANIMAL_SEARCH_FOOD] > 0) {
        return false;
    }
    Seeker seeker = {context, (AnimalSpecies)animal->species, true, def->flight_distance};
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
    begin(animal, ANIMAL_TASK_EAT, found);
    return true;
}

static bool try_drink(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    if (animal->search_cooldown[ANIMAL_SEARCH_WATER] > 0) {
        return false;
    }
    Seeker seeker = {context, (AnimalSpecies)animal->species, false, def->flight_distance};
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
        !path_walker_can_stand(PATH_WALKER_ANIMAL, map, x, y, z) ||
        near_threat(context, x, y, z, def->flight_distance) ||
        !find_path(context, tile_of(animal), goal, ANIMAL_WANDER_MAX_COST, &animal->path)) {
        path_clear(&animal->path);
        return false;
    }
    begin(animal, ANIMAL_TASK_WANDER, goal);
    return true;
}

/* Goes for water or food when it has less than these, the most pressing
 * first. */
static bool try_needs(Animal *animal, const AnimalDef *def, const AnimalContext *context,
                      float food_below, float water_below)
{
    float food = animal->needs[ANIMAL_NEED_FOOD];
    float water = animal->needs[ANIMAL_NEED_WATER];
    bool hungry = food < food_below, thirsty = water < water_below;
    if (thirsty && (!hungry || water <= food)) {
        return try_drink(animal, def, context) || (hungry && try_eat(animal, def, context));
    }
    if (hungry) {
        return try_eat(animal, def, context) || (thirsty && try_drink(animal, def, context));
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
    if (try_needs(animal, def, context, ANIMAL_URGENT_NEED, ANIMAL_URGENT_NEED)) {
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
    if (try_needs(animal, def, context, ANIMAL_HUNGRY, ANIMAL_THIRSTY)) {
        return;
    }
    if (rng_chance(context->rng, 0.5f) &&
        try_wander(animal, def, leader != NULL ? tile_of(leader) : tile_of(animal), context)) {
        return;
    }
    stand(animal, random_idle_ticks(context->rng));
}

/* Eats what the tile offers: a bite. */
static void bite(Animal *animal, const AnimalDef *def, Map *map, AnimalFood food)
{
    size_t index = map_index(map, animal->x, animal->y, animal->z);
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
            map_remove_plant(map, animal->x, animal->y, animal->z); /* The patch is gone. */
        } else if (plant_type->dug_whole) {
            plant->growth = 0.0f; /* Grows again from what is left in the ground. */
            memset(plant->stock, 0, sizeof(plant->stock));
        }
        break;
    }
    case ANIMAL_FOOD_NUTS: {
        int taken = map_take_litter_count(map, animal->x, animal->y, animal->z, ITEM_HAZELNUTS,
                                          def->bite_items);
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

static void eat(Animal *animal, const AnimalDef *def, const AnimalContext *context)
{
    Map *map = context->map;
    bool there = animal->x == animal->target.x && animal->y == animal->target.y &&
                 animal->z == animal->target.z &&
                 animal_food_at((AnimalSpecies)animal->species, map, animal->x, animal->y,
                                animal->z) != ANIMAL_FOOD_NONE;
    if (!there) {
        /* The way was cut, or the food is gone (eaten, picked): elsewhere. */
        if (!try_eat(animal, def, context)) {
            stand(animal, random_idle_ticks(context->rng));
        }
        return;
    }
    if (++animal->timer < ANIMAL_BITE_TICKS) {
        return;
    }
    animal->timer = 0;
    bite(animal, def, map,
         animal_food_at((AnimalSpecies)animal->species, map, animal->x, animal->y, animal->z));
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
    if (rest >= 1.0f || (!resting_hours && rest >= ANIMAL_RESTED) || pressed) {
        stand(animal, 0);
    }
}

/* Moves one tick along the path. */
static void walk(Animal *animal, const AnimalDef *def, const AnimalContext *context)
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
    float cost = map_move_cost(map, next.x, next.y, next.z);
    if (next.z != animal->z) {
        cost *= MAP_CLIMB_COST;
    } else if (next.x != animal->x && next.y != animal->y) {
        cost *= SQRT2;
    }
    if (next.x != animal->x) {
        animal->facing_left = next.x < animal->x;
    }
    float speed = animal->task == ANIMAL_TASK_FLEE ? def->flee_speed : def->walk_speed;
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
    Seeker seeker = {context, (AnimalSpecies)animal->species, false,
                     def->flight_distance + ANIMAL_FLEE_MARGIN};
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
    bool seen = false;
    for (int i = 0; i < context->threat_count && !seen; i++) {
        seen = animal_notices((AnimalSpecies)animal->species, context->map, animal->x, animal->y,
                              animal->z, context->threats[i], animal->task == ANIMAL_TASK_SLEEP);
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
        }
    }
}

/* ---- Tick ----------------------------------------------------------------- */

static void update_needs(Animal *animal, const AnimalDef *def)
{
    const float hour = (float)TICKS_PER_HOUR;
    float *needs = animal->needs;
    needs[ANIMAL_NEED_FOOD] =
        clampf(needs[ANIMAL_NEED_FOOD] - 1.0f / (def->hunger_hours * hour), 0.0f, 1.0f);
    needs[ANIMAL_NEED_WATER] =
        clampf(needs[ANIMAL_NEED_WATER] - 1.0f / (def->thirst_hours * hour), 0.0f, 1.0f);
    float rest = animal->task == ANIMAL_TASK_SLEEP ? 1.0f / (def->sleep_hours * hour)
                                                   : -1.0f / (def->awake_hours * hour);
    needs[ANIMAL_NEED_REST] = clampf(needs[ANIMAL_NEED_REST] + rest, 0.0f, 1.0f);
}

static void tick_one(Animals *animals, uint32_t slot, const AnimalContext *context)
{
    Animal *animal = &animals->list[slot];
    const AnimalDef *def = &ANIMAL_DEFS[animal->species];
    update_needs(animal, def);
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
    /* On a tile center, and not running already: a look around now and then. */
    bool running = animal->task == ANIMAL_TASK_FLEE && animal_is_moving(animal);
    if (animal->move_progress <= 0.0f && !running && (animal->alarmed || animal->alert_wait == 0)) {
        animal->alert_wait = ANIMAL_ALERT_TICKS;
        look_out(animals, slot, context);
    }
    if (animal_is_moving(animal)) {
        walk(animal, def, context);
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
    case ANIMAL_TASK_EAT: eat(animal, def, context); break;
    case ANIMAL_TASK_DRINK: drink(animal, context); break;
    case ANIMAL_TASK_SLEEP: sleep_on(animal, context); break;
    case ANIMAL_TASK_COUNT: break;
    }
}

void animals_tick(Animals *animals, const AnimalContext *context)
{
    /* Fixed slot order keeps the simulation deterministic. */
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        if (!entity_id_is_none(animals_id_at(animals, slot))) {
            tick_one(animals, slot, context);
        }
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
    }
    return hash;
}
