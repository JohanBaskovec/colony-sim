/*
 * Wild animals: they live their own life. Each has three needs (food, water,
 * rest, from 1 down to 0 like the settlers') and, standing on a tile center,
 * decides what to do: drink at the nearest water, eat, sleep when tired or
 * during the hours its species rests, go back to its herd, or wander.
 *
 * Only plant eaters for now. What a species eats is in its AnimalDef: grass,
 * grazed down bite by bite (Plant.growth), the food wild plants bear (the same
 * stock the settlers pick: animals compete with gathering), roots dug up by
 * those that dig, and the nuts lying under the trees. An animal goes for a
 * treat close by before it grazes.
 *
 * Animals fear the settlers: one that sees a settler within its species'
 * flight distance, or hears one much closer without seeing it, runs off, and
 * warns its herd; none feeds, drinks nor wanders near one. Settlers take no
 * notice of them.
 *
 * Herds: an animal follows a leader (itself when it lives alone). A herd is
 * made of a leader and those who follow it; followers stay around it, and
 * the herd roams as its leader does. Herd sizes follow the species: hinds
 * and sows live in herds, stags in small groups, boars and hares alone.
 *
 * Animals walk on four legs (PATH_WALKER_ANIMAL): slopes, but no ladder, no
 * tree and no door. They cannot die yet (health comes later), nor breed.
 *
 * Animals are stored here in a fixed array managed by a Pool and referenced
 * by EntityId; the Game owns the Animals and ticks them after the settlers.
 */
#ifndef COLONY_CORE_ANIMAL_H
#define COLONY_CORE_ANIMAL_H

#include "core/game_clock.h"
#include "core/map.h"
#include "core/pathfinding.h"
#include "core/pool.h"
#include "core/region.h"
#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

#define ANIMAL_MAX 256

typedef enum AnimalSpecies {
    ANIMAL_ROE_DEER,
    ANIMAL_RED_DEER,
    ANIMAL_BOAR,
    ANIMAL_HARE,
    ANIMAL_SPECIES_COUNT
} AnimalSpecies;

typedef enum AnimalSex { ANIMAL_FEMALE, ANIMAL_MALE, ANIMAL_SEX_COUNT } AnimalSex;

typedef enum AnimalNeed {
    ANIMAL_NEED_FOOD,
    ANIMAL_NEED_WATER,
    ANIMAL_NEED_REST,
    ANIMAL_NEED_COUNT
} AnimalNeed;

typedef enum AnimalTask {
    ANIMAL_TASK_IDLE,   /* Standing there, looking around. */
    ANIMAL_TASK_WANDER, /* Walking a little way, or back to its herd. */
    ANIMAL_TASK_EAT,    /* Walking to food, then eating. */
    ANIMAL_TASK_DRINK,  /* Walking to water, then drinking. */
    ANIMAL_TASK_SLEEP,
    ANIMAL_TASK_FLEE, /* Running away from a settler. */
    ANIMAL_TASK_COUNT
} AnimalTask;

/* What an animal looks for, each with its own delay after a failure. */
typedef enum AnimalSearch {
    ANIMAL_SEARCH_FOOD,
    ANIMAL_SEARCH_WATER,
    ANIMAL_SEARCH_COUNT
} AnimalSearch;

/* What a tile offers a species to eat (the best of it). */
typedef enum AnimalFood {
    ANIMAL_FOOD_NONE,
    ANIMAL_FOOD_GRASS, /* Grass tall enough to graze. */
    ANIMAL_FOOD_NUTS,  /* Nuts lying on the ground. */
    ANIMAL_FOOD_PLANT, /* The food a plant bears (fruit, leaves, roots). */
} AnimalFood;

/* A set of plant types (bit t: PlantType t). */
typedef uint32_t AnimalPlantMask;
_Static_assert(PLANT_COUNT <= 32, "plant types must fit in AnimalPlantMask");

typedef struct AnimalDef {
    const char *id;
    const char *name_key; /* The species. */
    /* The animal by its sex (a hind, a stag), indexed by AnimalSex. */
    const char *sex_name_keys[ANIMAL_SEX_COUNT];
    float walk_speed; /* Tiles per second on terrain of cost 1. */
    float flee_speed;
    /* Hours for each need to go from 1 to 0 (rest: awake), and hours of sleep
     * that bring the rest back from 0 to 1. */
    float hunger_hours;
    float thirst_hours;
    float awake_hours;
    float sleep_hours;
    /* The hours of the day it rests in (from `rest_from` to `rest_until`,
     * through midnight when the second is smaller): it goes to sleep then as
     * soon as it is a little tired. */
    int rest_from;
    int rest_until;
    /* Tiles from which it flees a settler it sees (half as far asleep). */
    int flight_distance;
    /* Herds: females live in herds of `herd_min` to `herd_max`, males in
     * groups of 1 to `male_group_max`; `male_share` of the animals are males. */
    int herd_min;
    int herd_max;
    int male_group_max;
    float male_share;
    /* Animals per 10,000 tiles of land when a world is populated at density 1. */
    float density;
    /* Grass: the growth it needs to graze a tile, the growth a bite takes
     * and the food a whole tile of grass (growth 1) is worth (0: no grazing). */
    float graze_min_growth;
    float graze_bite;
    float graze_value;
    /* Plants whose food it eats, whether it digs roots up (PlantDef.food_dug:
     * left alone otherwise), whether it eats the nuts lying about, items a
     * bite takes, and the food a unit of nutrition (ItemDef.nutrition, what
     * a settler gets of it) is worth to it: small animals get more of it. */
    AnimalPlantMask plants;
    bool digs;
    bool eats_nuts;
    int bite_items;
    float food_value;
} AnimalDef;

typedef struct Animal {
    uint8_t species; /* AnimalSpecies */
    uint8_t sex;     /* AnimalSex */
    int x;           /* Current tile. */
    int y;
    int z;
    Path path;
    int path_index;      /* Next step of `path` to walk to. */
    float move_progress; /* 0..1 from the current tile to the next step. */
    uint8_t task;        /* AnimalTask */
    /* Where the food or the water is (ANIMAL_TASK_EAT, ANIMAL_TASK_DRINK). */
    TilePos target;
    /* Idle countdown (ANIMAL_TASK_IDLE), progress of a bite or of a drink. */
    int timer;
    float needs[ANIMAL_NEED_COUNT];
    /* The animal its herd follows: itself for a leader and for a loner. */
    EntityId leader;
    /* A herd mate ran off from a settler: so will it, the next time it looks
     * around. */
    bool alarmed;
    int alert_wait;    /* Ticks before it looks around for settlers again. */
    int flee_cooldown; /* Ticks before a cornered animal looks for a way out again. */
    int search_cooldown[ANIMAL_SEARCH_COUNT]; /* Ticks before searching again. */
    int search_failures[ANIMAL_SEARCH_COUNT]; /* Consecutive failed searches. */
    /* Which way it last stepped sideways: cosmetic, not part of the hash. */
    bool facing_left;
} Animal;

typedef struct Animals {
    Pool pool;
    Animal list[ANIMAL_MAX]; /* Indexed by EntityId.index. */
} Animals;

/* What animals need of the world to live a tick. */
typedef struct AnimalContext {
    Map *map;
    Pathfinder *pathfinder;
    /* Connectivity regions (may be out of date, never wrong: core/region.h). */
    const Regions *regions;
    Rng *rng;
    uint64_t tick;
    /* Where the settlers stand: who the animals fear. */
    const TilePos *threats;
    int threat_count;
} AnimalContext;

/* Needs below which an animal goes to drink or eat, the food at which it
 * stops eating, and the rest below which it sleeps wherever it is, and
 * during the hours its species rests. */
#define ANIMAL_THIRSTY 0.4f
#define ANIMAL_HUNGRY 0.5f
#define ANIMAL_SATED 0.9f
#define ANIMAL_TIRED 0.2f
#define ANIMAL_REST_TIME_TIRED 0.85f
/* Hunger or thirst this low comes before everything else, sleep included: it
 * wakes a sleeper up. */
#define ANIMAL_URGENT_NEED 0.15f
/* A sleeper wakes up once rested this much when its resting hours are over
 * (fully rested otherwise). */
#define ANIMAL_RESTED 0.6f

/* Ticks a bite and a drink take. */
#define ANIMAL_BITE_TICKS 400
#define ANIMAL_DRINK_TICKS 300
/* An animal stands still for a while between two things (ticks, random). */
#define ANIMAL_IDLE_MIN_TICKS 120
#define ANIMAL_IDLE_MAX_TICKS 900
/* How far (travel cost) it looks for a treat (what it likes better than
 * grass: fruit, nuts, roots), for any food, and for water. */
#define ANIMAL_TREAT_SEARCH_MAX_COST 14.0f
#define ANIMAL_FOOD_SEARCH_MAX_COST 60.0f
#define ANIMAL_WATER_SEARCH_MAX_COST 250.0f
/* After a failed search it waits this long before the next one, times the
 * failures in a row (ANIMAL_SEARCH_MAX_FAILURES at most). */
#define ANIMAL_SEARCH_RETRY_TICKS (TICKS_PER_HOUR / 2)
#define ANIMAL_SEARCH_MAX_FAILURES 6
/* A stroll: this many tiles at most from where it stands (a herd's leader, a
 * loner) or from its leader (a follower), and the cost it accepts for it. */
#define ANIMAL_WANDER_RADIUS 7
#define ANIMAL_WANDER_MAX_COST 40.0f
/* A follower further than this from its leader walks back to it. */
#define ANIMAL_HERD_RADIUS 6
#define ANIMAL_HERD_MAX_COST 300.0f
/* How often (ticks) an animal looks around for settlers, and how long one
 * that found nowhere to run waits before it tries again. */
#define ANIMAL_ALERT_TICKS 20
#define ANIMAL_CORNERED_TICKS 600
/* Fleeing: it runs until every settler is this many tiles further than its
 * flight distance, within this travel cost; cornered, it stands still. */
#define ANIMAL_FLEE_MARGIN 8
#define ANIMAL_FLEE_MAX_COST 90.0f
/* Settlers further than this many levels up or down are not noticed. */
#define ANIMAL_ALERT_LEVELS 2
/* A settler this close is heard or smelled, seen or not (behind a trunk, a
 * wall): a share of the flight distance. */
#define ANIMAL_SCENT_SHARE 3
/* New herds of a generated world settle at least this far (tiles) from where
 * the settlers arrive. */
#define ANIMAL_SPAWN_CLEARANCE 30

const AnimalDef *animal_def(AnimalSpecies species);

/* Translation keys: the animal by its sex ("Hind"), what it is doing, a need. */
const char *animal_name_key(AnimalSpecies species, AnimalSex sex);
const char *animal_activity_key(const Animal *animal);
const char *animal_need_name_key(AnimalNeed need);

bool animals_init(Animals *animals);
void animals_free(Animals *animals);

/*
 * Creates an animal on a tile it can stand on, alone (its own leader), with
 * needs drawn from `rng`. Returns ENTITY_NONE on an invalid species or sex,
 * a tile no animal can stand on, or when there is no room left.
 */
EntityId animals_spawn(Animals *animals, const Map *map, AnimalSpecies species, AnimalSex sex,
                       int x, int y, int z, Rng *rng);
bool animals_remove(Animals *animals, EntityId id);

/* NULL when the handle is stale. */
Animal *animals_get(Animals *animals, EntityId id);
const Animal *animals_get_const(const Animals *animals, EntityId id);

/* Iteration: for (uint32_t i = 0; i < animals_slot_count(a); i++), skipping ENTITY_NONE. */
uint32_t animals_slot_count(const Animals *animals);
EntityId animals_id_at(const Animals *animals, uint32_t slot);

/* Makes an animal follow another one of its species (itself: it leaves its
 * herd). The leader followed is the head of that animal's own herd. Fails on
 * stale handles and across species. */
bool animals_set_leader(Animals *animals, EntityId id, EntityId leader);

/* The head of the animal's herd (itself for a leader, a loner, or when its
 * leader is gone); ENTITY_NONE for a stale handle. */
EntityId animals_leader(const Animals *animals, EntityId id);

/* How many animals are in its herd, itself included (0 for a stale handle). */
int animals_herd_size(const Animals *animals, EntityId id);

/* Whether an animal stands on the tile. */
bool animals_on_tile(const Animals *animals, int x, int y, int z);

/*
 * Populates a generated world: herds of every species over the land, `density`
 * times AnimalDef.density, each on ground that leads to water, and when
 * `avoid` is not NULL at least ANIMAL_SPAWN_CLEARANCE tiles from it. Returns
 * how many animals were created.
 */
int animals_populate(Animals *animals, const Map *map, Pathfinder *pathfinder,
                     const Regions *regions, Rng *rng, float density, const TilePos *avoid);

/* One tick of every animal, in slot order. */
void animals_tick(Animals *animals, const AnimalContext *context);

/* What the tile offers the species to eat now. */
AnimalFood animal_food_at(AnimalSpecies species, const Map *map, int x, int y, int z);

/* Whether the hour of the day (0..23) is one the species rests in. */
bool animal_rest_hour(AnimalSpecies species, int hour);

/* Whether an animal on (x, y, z) would see a settler on `threat`: within
 * `distance` tiles, at most ANIMAL_ALERT_LEVELS levels apart, with nothing
 * (rock, a wall, a door) on the straight line between them. */
bool animal_sees(const Map *map, int x, int y, int z, TilePos threat, int distance);

/* Whether an animal of the species standing on (x, y, z) notices a settler
 * on `threat`: seen within its flight distance, or within a share of it
 * (ANIMAL_SCENT_SHARE) seen or not; half as far for both when it is asleep. */
bool animal_notices(AnimalSpecies species, const Map *map, int x, int y, int z, TilePos threat,
                    bool asleep);

bool animal_is_moving(const Animal *animal);

/* Interpolated position of the animal's center, in world (tile) coordinates. */
void animal_position(const Animal *animal, float *x, float *y);

uint64_t animals_hash(const Animals *animals, uint64_t hash);

#endif /* COLONY_CORE_ANIMAL_H */
