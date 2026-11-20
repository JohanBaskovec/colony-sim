#include "core/animal.h"
#include "core/attributes.h"
#include "core/game.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <math.h>
#include <string.h>

/* The start of the first day of a game (the end of summer: brambles bear fruit). */
#define TODAY ((uint64_t)GAME_START_DAY * TICKS_PER_DAY)

/* A small game on flat, bare soil: no plant, no water, nobody. At noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_COBBLE) | ITEM_TYPE_BIT(ITEM_FLINT));
        map_clear_litter_at(map, i,
                            ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS) |
                                ITEM_TYPE_BIT(ITEM_HAZELNUTS));
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    game->clock.tick = TODAY + 12 * TICKS_PER_HOUR;
    return true;
}

/* An animal with every need satisfied, about to decide what to do. */
static Animal *spawn(Game *game, AnimalSpecies species, AnimalSex sex, int x, int y)
{
    Animal *animal =
        animals_get(&game->animals, spawn_ordinary_animal(game, species, sex, x, y, 0));
    if (animal != NULL) {
        for (int need = 0; need < ANIMAL_NEED_COUNT; need++) {
            animal->needs[need] = 1.0f;
        }
        animal->timer = 1;
    }
    return animal;
}

/* Whatever it was doing, it decides anew on the next tick. */
static void decide_now(Animal *animal)
{
    animal->task = ANIMAL_TASK_IDLE;
    animal->timer = 1;
    animal->path.count = 0;
    animal->path_index = 0;
    animal->move_progress = 0.0f;
}

static EntityId id_of(const Game *game, const Animal *animal)
{
    return animals_id_at(&game->animals, (uint32_t)(animal - game->animals.list));
}

static void grow_grass(Game *game, int x0, int y0, int x1, int y1, float growth)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            game->map.plants[map_index(&game->map, x, y, 0)] =
                (Plant){.type = PLANT_GRASS, .growth = growth};
        }
    }
}

/* A grown plant on a tile, bearing what it bears on the day of the game. */
static Plant *put_plant(Game *game, int x, int y, PlantType type)
{
    Plant *plant = &game->map.plants[map_index(&game->map, x, y, 0)];
    *plant = (Plant){.type = (uint8_t)type, .growth = 1.0f};
    plant_settle(plant, game_day_of_year(game->clock.tick));
    return plant;
}

/* Holds a settler where it stands (it would wander off otherwise). */
static void hold(Colonist *settler, int x, int y)
{
    settler->x = x;
    settler->y = y;
    settler->path.count = 0;
    settler->move_progress = 0.0f;
}

static void run(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

static float distance_between(const Animal *animal, int x, int y)
{
    float dx = (float)(animal->x - x), dy = (float)(animal->y - y);
    return sqrtf(dx * dx + dy * dy);
}

static void test_species_definitions(void)
{
    for (int species = 0; species < ANIMAL_SPECIES_COUNT; species++) {
        const AnimalDef *def = animal_def((AnimalSpecies)species);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL && def->name_key != NULL);
        CHECK(animal_name_key((AnimalSpecies)species, ANIMAL_FEMALE) != NULL);
        CHECK(animal_name_key((AnimalSpecies)species, ANIMAL_MALE) != NULL);
        /* Nobody catches one on foot. */
        CHECK(def->flee_speed > COLONIST_WALK_SPEED && def->walk_speed > 0.0f);
        CHECK(def->hunger_hours > 0.0f && def->thirst_hours > 0.0f && def->awake_hours > 0.0f &&
              def->sleep_hours > 0.0f);
        CHECK(def->flight_distance > 0);
        CHECK(def->herd_min >= 1 && def->herd_max >= def->herd_min && def->male_group_max >= 1);
        CHECK(def->male_share > 0.0f && def->male_share < 1.0f);
        CHECK(def->density > 0.0f);
        if (def->carnivore) {
            /* Meat eaters: they hunt, and bite carcasses. */
            CHECK(def->prey != 0 && def->hunt_range > 0 && def->bite_kg > 0.0f &&
                  def->stomach_kg > def->bite_kg);
            CHECK(combat_choice_count(def->attacks) > 0);
            CHECK(def->plants == 0 && !def->eats_fallen && def->graze_value == 0.0f);
            CHECK((def->prey & ANIMAL_SPECIES_BIT(species)) == 0);
        } else {
            /* Plant eaters: they graze. */
            CHECK(def->graze_value > 0.0f && def->graze_bite > 0.0f && def->food_value > 0.0f);
            CHECK(def->bite_items >= 1);
        }
        CHECK(def->sprint_seconds > 0.0f && def->agility > 0.0f && def->nerve >= 0.0f);
        CHECK(item_def(def->carcass) != NULL &&
              item_def(def->carcass)->category == ITEM_CATEGORY_BODIES);
        int rest_hours = 0;
        for (int hour = 0; hour < HOURS_PER_DAY; hour++) {
            rest_hours += animal_rest_hour((AnimalSpecies)species, hour) ? 1 : 0;
        }
        CHECK(rest_hours >= 4 && rest_hours <= 12);
    }
    CHECK(animal_def(ANIMAL_SPECIES_COUNT) == NULL);
    CHECK(animal_name_key(ANIMAL_SPECIES_COUNT, ANIMAL_MALE) == NULL);
    CHECK(animal_name_key(ANIMAL_HARE, ANIMAL_SEX_COUNT) == NULL);
    /* Deer rest at night, boars and hares by day. */
    CHECK(animal_rest_hour(ANIMAL_RED_DEER, 23) && animal_rest_hour(ANIMAL_RED_DEER, 2));
    CHECK(!animal_rest_hour(ANIMAL_RED_DEER, 12));
    CHECK(animal_rest_hour(ANIMAL_BOAR, 12) && !animal_rest_hour(ANIMAL_BOAR, 23));
    CHECK(animal_rest_hour(ANIMAL_HARE, 12) && !animal_rest_hour(ANIMAL_HARE, 2));
    for (int need = 0; need < ANIMAL_NEED_COUNT; need++) {
        CHECK(animal_need_name_key((AnimalNeed)need) != NULL);
    }
}

static void test_spawn_and_remove(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animals *animals = &game.animals;
    CHECK_EQ_INT(animals->pool.count, 0);
    map_set_rock(&game.map, 5, 5, 0, ROCK_GRANITE);
    map_set_structure(&game.map, 6, 5, 0, STRUCTURE_DOOR);
    CHECK(entity_id_is_none(spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 5, 5, 0)));
    CHECK(entity_id_is_none(spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 6, 5, 0)));
    CHECK(entity_id_is_none(spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, -1, 5, 0)));
    CHECK(entity_id_is_none(spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 7, 5, 1)));
    CHECK(entity_id_is_none(
        spawn_ordinary_animal(&game, ANIMAL_SPECIES_COUNT, ANIMAL_MALE, 7, 5, 0)));
    CHECK(entity_id_is_none(spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_SEX_COUNT, 7, 5, 0)));

    EntityId id = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 7, 5, 0);
    REQUIRE(!entity_id_is_none(id));
    const Animal *animal = game_animal_const(&game, id);
    REQUIRE(animal != NULL);
    CHECK_EQ_INT(animal->species, ANIMAL_RED_DEER);
    CHECK_EQ_INT(animal->sex, ANIMAL_FEMALE);
    CHECK(animal->x == 7 && animal->y == 5 && animal->z == 0);
    for (int need = 0; need < ANIMAL_NEED_COUNT; need++) {
        CHECK(animal->needs[need] > 0.5f && animal->needs[need] <= 1.0f);
    }
    CHECK(entity_id_equal(animals_leader(animals, id), id));
    CHECK_EQ_INT(animals_herd_size(animals, id), 1);
    CHECK(animals_on_tile(animals, 7, 5, 0));
    CHECK(!animals_on_tile(animals, 8, 5, 0));
    CHECK(!animal_is_moving(animal));
    float x, y;
    animal_position(animal, &x, &y);
    CHECK_NEAR(x, 7.5, 1e-6);
    CHECK_NEAR(y, 5.5, 1e-6);

    CHECK(animals_remove(animals, id));
    CHECK(!animals_remove(animals, id));
    CHECK(game_animal_const(&game, id) == NULL);
    CHECK_EQ_INT(animals_herd_size(animals, id), 0);
    CHECK(entity_id_is_none(animals_leader(animals, id)));
    CHECK(!animals_on_tile(animals, 7, 5, 0));

    /* The pool fills up, and no more. */
    int made = 0;
    for (int i = 0; i < ANIMAL_MAX + 5; i++) {
        made += entity_id_is_none(
                    spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_FEMALE, 10 + i % 40, 10, 0))
                    ? 0
                    : 1;
    }
    CHECK_EQ_INT(made, ANIMAL_MAX);
    game_shutdown(&game);
}

static void test_herds(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animals *animals = &game.animals;
    EntityId a = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 10, 10, 0);
    EntityId b = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 11, 10, 0);
    EntityId c = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 12, 10, 0);
    EntityId d = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 13, 10, 0);
    EntityId boar = spawn_ordinary_animal(&game, ANIMAL_BOAR, ANIMAL_MALE, 14, 10, 0);
    CHECK(animals_set_leader(animals, b, a));
    CHECK(animals_set_leader(animals, c, b)); /* Follows b's leader. */
    CHECK(entity_id_equal(animals_leader(animals, c), a));
    CHECK_EQ_INT(animals_herd_size(animals, a), 3);
    CHECK_EQ_INT(animals_herd_size(animals, c), 3);
    CHECK_EQ_INT(animals_herd_size(animals, d), 1);
    CHECK(!animals_set_leader(animals, boar, a)); /* Another species. */
    CHECK(!animals_set_leader(animals, ENTITY_NONE, a));
    CHECK(!animals_set_leader(animals, d, ENTITY_NONE));
    CHECK(animals_set_leader(animals, a, c)); /* It leads c already. */
    CHECK(entity_id_equal(animals_leader(animals, a), a));

    /* A leader joining another herd brings its own along. */
    CHECK(animals_set_leader(animals, a, d));
    CHECK(entity_id_equal(animals_leader(animals, a), d));
    CHECK(entity_id_equal(animals_leader(animals, b), d));
    CHECK(entity_id_equal(animals_leader(animals, c), d));
    CHECK_EQ_INT(animals_herd_size(animals, d), 4);

    /* One leaves; then the leader does, and the first follower takes over. */
    CHECK(animals_set_leader(animals, c, c));
    CHECK_EQ_INT(animals_herd_size(animals, c), 1);
    CHECK_EQ_INT(animals_herd_size(animals, d), 3);
    CHECK(animals_set_leader(animals, d, d));
    CHECK_EQ_INT(animals_herd_size(animals, d), 1);
    CHECK(entity_id_equal(animals_leader(animals, a), a));
    CHECK(entity_id_equal(animals_leader(animals, b), a));
    CHECK_EQ_INT(animals_herd_size(animals, b), 2);

    /* The leader gone: the herd lives on under one of its own. */
    CHECK(animals_set_leader(animals, c, a));
    CHECK(animals_remove(animals, a));
    CHECK(entity_id_equal(animals_leader(animals, b), b));
    CHECK(entity_id_equal(animals_leader(animals, c), b));
    CHECK_EQ_INT(animals_herd_size(animals, c), 2);
    game_shutdown(&game);
}

static void test_needs_go_down(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *deer = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 20, 20);
    REQUIRE(deer != NULL);
    const AnimalDef *def = animal_def(ANIMAL_RED_DEER);
    run(&game, TICKS_PER_HOUR);
    CHECK_NEAR(deer->needs[ANIMAL_NEED_FOOD], 1.0 - 1.0 / def->hunger_hours, 1e-3);
    CHECK_NEAR(deer->needs[ANIMAL_NEED_WATER], 1.0 - 1.0 / def->thirst_hours, 1e-3);
    CHECK_NEAR(deer->needs[ANIMAL_NEED_REST], 1.0 - 1.0 / def->awake_hours, 1e-3);
    /* Never below zero, and nothing to die of yet. */
    deer->needs[ANIMAL_NEED_FOOD] = 0.0001f;
    deer->needs[ANIMAL_NEED_WATER] = 0.0001f;
    run(&game, 200);
    CHECK(deer->needs[ANIMAL_NEED_FOOD] == 0.0f && deer->needs[ANIMAL_NEED_WATER] == 0.0f);
    CHECK_EQ_INT(game.animals.pool.count, 1);
    game_shutdown(&game);
}

static void test_grazing(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    const Map *map = &game.map;
    CHECK(animal_food_at(ANIMAL_RED_DEER, map, 20, 20, 0) == ANIMAL_FOOD_NONE);
    grow_grass(&game, 30, 20, 32, 22, 1.0f);
    CHECK(animal_food_at(ANIMAL_RED_DEER, map, 30, 20, 0) == ANIMAL_FOOD_GRASS);
    CHECK(animal_food_at(ANIMAL_RED_DEER, map, -1, 20, 0) == ANIMAL_FOOD_NONE);
    CHECK(animal_food_at(ANIMAL_SPECIES_COUNT, map, 30, 20, 0) == ANIMAL_FOOD_NONE);
    /* Grass too short to graze. */
    grow_grass(&game, 40, 40, 40, 40, 0.1f);
    CHECK(animal_food_at(ANIMAL_RED_DEER, map, 40, 40, 0) == ANIMAL_FOOD_NONE);
    game.map.plants[map_index(map, 40, 40, 0)].type = PLANT_NONE;

    Animal *deer = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 20, 20);
    REQUIRE(deer != NULL);
    run(&game, 5);
    CHECK(deer->task != ANIMAL_TASK_EAT); /* Not hungry. */
    deer->needs[ANIMAL_NEED_FOOD] = 0.3f;
    decide_now(deer);
    float before = 0.0f;
    for (int y = 20; y <= 22; y++) {
        for (int x = 30; x <= 32; x++) {
            before += map->plants[map_index(map, x, y, 0)].growth;
        }
    }
    run(&game, 3);
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_EAT);
    CHECK(animal_is_moving(deer));
    CHECK(strcmp(animal_activity_key(deer), "animal.activity.to_food") == 0);
    bool ate = false;
    for (int i = 0; i < 40000 && deer->needs[ANIMAL_NEED_FOOD] < ANIMAL_SATED - 0.01f; i++) {
        game_tick(&game);
        if (deer->task == ANIMAL_TASK_EAT && !animal_is_moving(deer)) {
            ate = true;
            CHECK(deer->x >= 30 && deer->x <= 32 && deer->y >= 20 && deer->y <= 22);
            CHECK(strcmp(animal_activity_key(deer), "animal.activity.eat") == 0);
        }
    }
    CHECK(ate);
    CHECK(deer->needs[ANIMAL_NEED_FOOD] >= ANIMAL_SATED - 0.01f);
    float after = 0.0f;
    for (int y = 20; y <= 22; y++) {
        for (int x = 30; x <= 32; x++) {
            after += map->plants[map_index(map, x, y, 0)].growth;
        }
    }
    /* The grass went down, bite by bite (it also grew a little meanwhile). */
    CHECK(after < before - 0.5f);
    run(&game, 2);
    CHECK(deer->task != ANIMAL_TASK_EAT); /* Sated. */
    game_shutdown(&game);
}

/* The wild berries, hips and leaves feed the animals of the woods: each is
 * eaten where it bears by some species, and not by one that does not eat it. */
static void test_animals_eat_the_wild_fruit(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    const struct {
        PlantType type;
        int day;
    } fruits[] = {{PLANT_BILBERRY, SEASON_DAY(SEASON_SUMMER, 9)},
                  {PLANT_WILD_STRAWBERRY, SEASON_DAY(SEASON_SUMMER, 3)},
                  {PLANT_DOG_ROSE, SEASON_DAY(SEASON_AUTUMN, 10)},
                  {PLANT_WOOD_SORREL, SEASON_DAY(SEASON_SUMMER, 3)}};
    for (size_t i = 0; i < sizeof(fruits) / sizeof(fruits[0]); i++) {
        Plant *plant = &game.map.plants[map_index(&game.map, 26, 20, 0)];
        *plant = (Plant){.type = (uint8_t)fruits[i].type, .growth = 1.0f};
        plant_settle(plant, fruits[i].day);
        REQUIRE(plant_has_food(plant));
        bool eaten = false;
        for (int species = 0; species < ANIMAL_SPECIES_COUNT; species++) {
            bool eats = (animal_def((AnimalSpecies)species)->plants & (1u << fruits[i].type)) != 0;
            CHECK((animal_food_at((AnimalSpecies)species, &game.map, 26, 20, 0) ==
                   ANIMAL_FOOD_PLANT) == eats);
            eaten = eaten || eats;
        }
        CHECK(eaten);
    }
    game_shutdown(&game);
}

static void test_treats_before_grass(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    grow_grass(&game, 18, 18, 22, 22, 1.0f);
    Plant *bramble = put_plant(&game, 26, 20, PLANT_BRAMBLE);
    REQUIRE(plant_has_food(bramble));
    int berries = plant_stock_available(bramble, 0);
    CHECK(animal_food_at(ANIMAL_ROE_DEER, &game.map, 26, 20, 0) == ANIMAL_FOOD_PLANT);
    CHECK(animal_food_at(ANIMAL_HARE, &game.map, 26, 20, 0) ==
          ANIMAL_FOOD_NONE); /* Not its food. */

    Animal *deer = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 20, 20);
    REQUIRE(deer != NULL);
    deer->needs[ANIMAL_NEED_FOOD] = 0.3f;
    run(&game, 3);
    /* Grass under its feet, but the blackberries close by first. */
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_EAT);
    CHECK(deer->target.x == 26 && deer->target.y == 20);
    for (int i = 0; i < 6000 && plant_stock_available(bramble, 0) == berries; i++) {
        game_tick(&game);
    }
    /* A bite: the settlers will find fewer. */
    CHECK_EQ_INT(plant_stock_available(bramble, 0),
                 berries - animal_def(ANIMAL_ROE_DEER)->bite_items);
    CHECK(deer->needs[ANIMAL_NEED_FOOD] > 0.3f);

    /* A treat too far away is not worth the walk: grass will do. */
    Animal *hind = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 20, 21);
    REQUIRE(hind != NULL);
    put_plant(&game, 60, 60, PLANT_BRAMBLE);
    hind->needs[ANIMAL_NEED_FOOD] = 0.3f;
    plant_take_stock(bramble, 0, 100); /* The near one is empty now. */
    run(&game, 3);
    CHECK_EQ_INT(hind->task, ANIMAL_TASK_EAT);
    CHECK(hind->target.x >= 18 && hind->target.x <= 22);
    game_shutdown(&game);
}

static void test_roots_nuts_and_mushrooms(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    /* Autumn: wild carrots can be dug up, mushrooms are out. */
    game.clock.tick = (uint64_t)SEASON_DAY(SEASON_AUTUMN, 5) * TICKS_PER_DAY + 20 * TICKS_PER_HOUR;
    Plant *carrot = put_plant(&game, 22, 20, PLANT_WILD_CARROT);
    REQUIRE(plant_has_food(carrot));
    CHECK(animal_food_at(ANIMAL_BOAR, map, 22, 20, 0) == ANIMAL_FOOD_PLANT);
    CHECK(animal_food_at(ANIMAL_RED_DEER, map, 22, 20, 0) == ANIMAL_FOOD_NONE);
    CHECK(animal_food_at(ANIMAL_HARE, map, 22, 20, 0) == ANIMAL_FOOD_NONE);
    Animal *boar = spawn(&game, ANIMAL_BOAR, ANIMAL_MALE, 20, 20);
    REQUIRE(boar != NULL);
    boar->needs[ANIMAL_NEED_FOOD] = 0.3f;
    for (int i = 0; i < 4000 && carrot->growth > 0.0f; i++) {
        game_tick(&game);
    }
    /* Dug up whole: it grows again from what is left in the ground. */
    CHECK(carrot->type == PLANT_WILD_CARROT && carrot->growth == 0.0f);
    CHECK(!plant_has_food(carrot));
    CHECK(boar->needs[ANIMAL_NEED_FOOD] > 0.3f);

    /* Nuts lying under the trees. */
    for (int i = 0; i < 6; i++) {
        map_add_litter(map, 20, 24, 0, ITEM_HAZELNUTS);
    }
    CHECK(animal_food_at(ANIMAL_BOAR, map, 20, 24, 0) == ANIMAL_FOOD_FALLEN);
    CHECK(animal_food_at(ANIMAL_RED_DEER, map, 20, 24, 0) == ANIMAL_FOOD_FALLEN);
    CHECK(animal_food_at(ANIMAL_HARE, map, 20, 24, 0) == ANIMAL_FOOD_NONE);
    boar->needs[ANIMAL_NEED_FOOD] = 0.3f;
    for (int i = 0; i < 6000 && map_litter(map, 20, 24, 0, ITEM_HAZELNUTS) == 6; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(map_litter(map, 20, 24, 0, ITEM_HAZELNUTS),
                 6 - animal_def(ANIMAL_BOAR)->bite_items);
    map_take_litter(map, 20, 24, 0, ITEM_HAZELNUTS);
    /* Fallen fruit too. */
    for (int i = 0; i < 6; i++) {
        map_add_litter(map, 20, 24, 0, ITEM_APPLES);
    }
    CHECK(animal_food_at(ANIMAL_BOAR, map, 20, 24, 0) == ANIMAL_FOOD_FALLEN);
    CHECK(animal_food_at(ANIMAL_HARE, map, 20, 24, 0) == ANIMAL_FOOD_NONE);
    boar->needs[ANIMAL_NEED_FOOD] = 0.3f;
    for (int i = 0; i < 6000 && map_litter(map, 20, 24, 0, ITEM_APPLES) == 6; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(map_litter(map, 20, 24, 0, ITEM_APPLES), 6 - animal_def(ANIMAL_BOAR)->bite_items);
    CHECK(boar->needs[ANIMAL_NEED_FOOD] > 0.3f);
    map_take_litter(map, 20, 24, 0, ITEM_APPLES);

    /* A mushroom patch eaten up is gone. */
    Plant *mushrooms = put_plant(&game, 20, 28, PLANT_MUSHROOMS);
    REQUIRE(plant_has_food(mushrooms));
    boar->needs[ANIMAL_NEED_FOOD] = 0.2f;
    for (int i = 0; i < 8000 && mushrooms->type == PLANT_MUSHROOMS; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(mushrooms->type, PLANT_NONE);
    game_shutdown(&game);
}

static void test_drinking(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *deer = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_MALE, 20, 20);
    REQUIRE(deer != NULL);
    deer->needs[ANIMAL_NEED_WATER] = 0.2f;
    run(&game, 3);
    /* No water anywhere: it does not search again at once. */
    CHECK(deer->task != ANIMAL_TASK_DRINK);
    CHECK(deer->search_cooldown[ANIMAL_SEARCH_WATER] > 0);
    CHECK_EQ_INT(deer->search_failures[ANIMAL_SEARCH_WATER], 1);

    for (int y = 30; y <= 33; y++) {
        for (int x = 30; x <= 33; x++) {
            map_set_terrain(&game.map, x, y, 0, TERRAIN_DEEP_WATER);
        }
    }
    deer->search_cooldown[ANIMAL_SEARCH_WATER] = 0;
    decide_now(deer);
    run(&game, 3);
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_DRINK);
    CHECK(strcmp(animal_activity_key(deer), "animal.activity.to_water") == 0);
    CHECK(map_is_drinking_spot(&game.map, deer->target.x, deer->target.y, deer->target.z));
    bool drank = false;
    for (int i = 0; i < 4000 && deer->needs[ANIMAL_NEED_WATER] < 0.99f; i++) {
        game_tick(&game);
        drank |= deer->task == ANIMAL_TASK_DRINK && !animal_is_moving(deer);
    }
    CHECK(drank);
    CHECK(deer->needs[ANIMAL_NEED_WATER] >= 0.99f);
    CHECK_EQ_INT(deer->search_failures[ANIMAL_SEARCH_WATER], 0);
    CHECK(deer->task != ANIMAL_TASK_DRINK);
    game_shutdown(&game);
}

static void test_sleep(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* Noon: a deer a little tired stays up, a boar goes to sleep. */
    Animal *deer = spawn(&game, ANIMAL_RED_DEER, ANIMAL_MALE, 20, 20);
    Animal *boar = spawn(&game, ANIMAL_BOAR, ANIMAL_MALE, 40, 40);
    REQUIRE(deer != NULL && boar != NULL);
    deer->needs[ANIMAL_NEED_REST] = 0.5f;
    boar->needs[ANIMAL_NEED_REST] = 0.5f;
    run(&game, 3);
    CHECK(deer->task != ANIMAL_TASK_SLEEP);
    CHECK_EQ_INT(boar->task, ANIMAL_TASK_SLEEP);
    CHECK(strcmp(animal_activity_key(boar), "animal.activity.sleep") == 0);
    float rest = boar->needs[ANIMAL_NEED_REST];
    run(&game, TICKS_PER_HOUR);
    CHECK_NEAR(boar->needs[ANIMAL_NEED_REST], rest + 1.0 / animal_def(ANIMAL_BOAR)->sleep_hours,
               1e-3);
    /* Worn out, anyone sleeps at any hour... */
    deer->needs[ANIMAL_NEED_REST] = 0.1f;
    for (int i = 0; i < 2000 && deer->task != ANIMAL_TASK_SLEEP; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_SLEEP);
    /* ...and gets up once rested enough, out of its resting hours. */
    for (int i = 0; i < 6 * TICKS_PER_HOUR && deer->task == ANIMAL_TASK_SLEEP; i++) {
        game_tick(&game);
    }
    CHECK(deer->task != ANIMAL_TASK_SLEEP);
    CHECK(deer->needs[ANIMAL_NEED_REST] >= ANIMAL_RESTED - 0.01f);
    CHECK(deer->needs[ANIMAL_NEED_REST] < 0.8f);

    /* At night the deer sleeps until it is fully rested. */
    game.clock.tick = TODAY + 23 * TICKS_PER_HOUR;
    deer->needs[ANIMAL_NEED_REST] = 0.7f;
    for (int i = 0; i < 2000 && deer->task != ANIMAL_TASK_SLEEP; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_SLEEP);
    for (int i = 0; i < 4 * TICKS_PER_HOUR && deer->task == ANIMAL_TASK_SLEEP; i++) {
        game_tick(&game);
    }
    CHECK(deer->task != ANIMAL_TASK_SLEEP);
    CHECK(deer->needs[ANIMAL_NEED_REST] >= 0.99f);

    /* Hunger wakes a sleeper up when there is something to eat. */
    game.clock.tick = TODAY + 23 * TICKS_PER_HOUR;
    grow_grass(&game, 18, 18, 22, 22, 1.0f);
    deer->needs[ANIMAL_NEED_REST] = 0.3f;
    for (int i = 0; i < 2000 && deer->task != ANIMAL_TASK_SLEEP; i++) {
        game_tick(&game);
    }
    REQUIRE(deer->task == ANIMAL_TASK_SLEEP);
    deer->needs[ANIMAL_NEED_FOOD] = 0.05f;
    run(&game, 3);
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_EAT);
    game_shutdown(&game);
}

static void test_sight(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    TilePos settler = {30, 20, 0};
    CHECK(animal_sees(map, 20, 20, 0, settler, 12));
    CHECK(!animal_sees(map, 20, 20, 0, settler, 9)); /* Too far. */
    CHECK(animal_sees(map, 24, 26, 0, settler, 12));
    CHECK(!animal_sees(map, 20, 20, 0, (TilePos){30, 20, 3}, 12)); /* Too high up. */
    /* Rock, a wall and a door stop the eye; a bed does not. */
    map_set_structure(map, 25, 20, 0, STRUCTURE_GROUND_BEDDING);
    CHECK(animal_sees(map, 20, 20, 0, settler, 12));
    map_set_structure(map, 25, 20, 0, STRUCTURE_WALL);
    CHECK(!animal_sees(map, 20, 20, 0, settler, 12));
    CHECK(animal_sees(map, 20, 23, 0, (TilePos){30, 23, 0}, 12)); /* Beside the wall. */
    map_set_structure(map, 25, 20, 0, STRUCTURE_DOOR);
    CHECK(!animal_sees(map, 20, 20, 0, settler, 12));
    map_set_structure(map, 25, 20, 0, STRUCTURE_NONE);
    map_set_rock(map, 27, 20, 0, ROCK_GRANITE);
    CHECK(!animal_sees(map, 20, 20, 0, settler, 12));
    /* What the settler stands on, or the animal, hides neither. */
    CHECK(animal_sees(map, 26, 20, 0, (TilePos){27, 20, 0}, 12));

    /* A roe deer notices a settler it sees within its flight distance, and
     * one very close that it does not see (heard, smelled); half as far
     * asleep. */
    int flight = animal_def(ANIMAL_ROE_DEER)->flight_distance;
    int scent = flight / ANIMAL_SCENT_SHARE;
    TilePos behind = {(int16_t)(27 + scent), 20, 0}; /* Behind the rock at (27, 20). */
    REQUIRE(scent >= 2 && !animal_sees(map, 26, 20, 0, behind, flight));
    CHECK(!animal_notices(ANIMAL_ROE_DEER, map, 26, 20, 0, behind, false));
    /* Three tiles away through the rock: close enough awake, not asleep. */
    CHECK(animal_notices(ANIMAL_ROE_DEER, map, 28, 20, 0, (TilePos){25, 20, 0}, false));
    CHECK(!animal_notices(ANIMAL_ROE_DEER, map, 28, 20, 0, (TilePos){25, 20, 0}, true));
    CHECK(animal_notices(ANIMAL_ROE_DEER, map, 28, 20, 0, (TilePos){27, 21, 0}, true));
    CHECK(animal_notices(ANIMAL_ROE_DEER, map, 20, 30, 0, (TilePos){(int16_t)(20 + flight), 30, 0},
                         false));
    CHECK(!animal_notices(ANIMAL_ROE_DEER, map, 20, 30, 0, (TilePos){(int16_t)(20 + flight), 30, 0},
                          true));
    CHECK(animal_notices(ANIMAL_ROE_DEER, map, 20, 30, 0,
                         (TilePos){(int16_t)(20 + flight / 2), 30, 0}, true));
    CHECK(!animal_notices(ANIMAL_ROE_DEER, map, 20, 30, 0, (TilePos){(int16_t)(21 + flight), 30, 0},
                          false));
    CHECK(!animal_notices(ANIMAL_SPECIES_COUNT, map, 20, 30, 0, (TilePos){21, 30, 0}, false));
    game_shutdown(&game);
}

static void test_flees_settlers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    const AnimalDef *def = animal_def(ANIMAL_ROE_DEER);
    Animal *near = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 30, 30);
    Animal *far = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 30, 55);
    /* Behind a wall, close by: it sees nobody. */
    Animal *hidden = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 20, 30);
    REQUIRE(near != NULL && far != NULL && hidden != NULL);
    for (int y = 20; y <= 40; y++) {
        map_set_structure(&game.map, 22, y, 0, STRUCTURE_WALL);
    }
    near->timer = far->timer = hidden->timer = 100000; /* Standing there. */
    run(&game, 2 * ANIMAL_ALERT_TICKS);
    CHECK_EQ_INT(near->task, ANIMAL_TASK_IDLE); /* Nobody about. */

    EntityId settler_id = game_spawn_colonist(&game, 25, 30, 0);
    Colonist *settler = game_colonist(&game, settler_id);
    REQUIRE(settler != NULL);
    for (int need = 0; need < NEED_COUNT; need++) {
        settler->needs[need] = 1.0f;
    }
    bool fled = false;
    for (int i = 0; i < 2 * ANIMAL_ALERT_TICKS; i++) {
        game_tick(&game);
        fled |= near->task == ANIMAL_TASK_FLEE;
        hold(settler, 25, 30); /* It only matters where it stands. */
    }
    CHECK(fled);
    CHECK(strcmp(animal_activity_key(near), "animal.activity.flee") == 0 ||
          near->task == ANIMAL_TASK_IDLE);
    for (int i = 0; i < 600; i++) {
        game_tick(&game);
        hold(settler, 25, 30);
    }
    /* Out of its flight distance, with room to spare, and calm again. */
    CHECK(distance_between(near, 25, 30) >= (float)(def->flight_distance + ANIMAL_FLEE_MARGIN));
    CHECK(near->task != ANIMAL_TASK_FLEE);
    CHECK(far->x == 30 && far->y == 55);
    CHECK(hidden->x == 20 && hidden->y == 30);
    game_shutdown(&game);
}

static void test_herd_runs_together(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animals *animals = &game.animals;
    const AnimalDef *def = animal_def(ANIMAL_RED_DEER);
    Animal *first = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 30, 30);
    /* Too far from the settler to see it, but of the herd; and a stranger
     * standing just as far. */
    Animal *mate = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 30, 41);
    Animal *stranger = spawn(&game, ANIMAL_RED_DEER, ANIMAL_MALE, 31, 41);
    REQUIRE(first != NULL && mate != NULL && stranger != NULL);
    CHECK(animals_set_leader(animals, id_of(&game, mate), id_of(&game, first)));
    first->timer = mate->timer = stranger->timer = 100000;
    Colonist *settler = game_colonist(&game, game_spawn_colonist(&game, 30, 24, 0));
    REQUIRE(settler != NULL);
    REQUIRE(
        !animal_sees(&game.map, mate->x, mate->y, 0, (TilePos){30, 24, 0}, def->flight_distance));
    bool mate_fled = false;
    for (int i = 0; i < 3 * ANIMAL_ALERT_TICKS; i++) {
        game_tick(&game);
        hold(settler, 30, 24);
        mate_fled |= mate->task == ANIMAL_TASK_FLEE;
    }
    CHECK(mate_fled);
    CHECK(!mate->alarmed);
    CHECK(stranger->x == 31 && stranger->y == 41 && stranger->task == ANIMAL_TASK_IDLE);
    game_shutdown(&game);
}

static void test_cornered(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* A pen of walls with a boar and a settler in it. */
    for (int i = 20; i <= 26; i++) {
        map_set_structure(&game.map, i, 20, 0, STRUCTURE_WALL);
        map_set_structure(&game.map, i, 26, 0, STRUCTURE_WALL);
        map_set_structure(&game.map, 20, i, 0, STRUCTURE_WALL);
        map_set_structure(&game.map, 26, i, 0, STRUCTURE_WALL);
    }
    Animal *boar = spawn(&game, ANIMAL_BOAR, ANIMAL_MALE, 22, 22);
    Colonist *settler = game_colonist(&game, game_spawn_colonist(&game, 24, 24, 0));
    REQUIRE(boar != NULL && settler != NULL);
    for (int i = 0; i < 3 * ANIMAL_ALERT_TICKS; i++) {
        game_tick(&game);
        hold(settler, 24, 24);
    }
    /* Nowhere to run: it stays, and does not look for a way out every time. */
    CHECK(boar->task != ANIMAL_TASK_FLEE);
    CHECK(boar->flee_cooldown > 0);
    CHECK(boar->x > 20 && boar->x < 26 && boar->y > 20 && boar->y < 26);
    game_shutdown(&game);
}

static void test_keeps_away_from_settlers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    const AnimalDef *def = animal_def(ANIMAL_RED_DEER);
    /* The only grass grows at a settler's feet. */
    grow_grass(&game, 30, 30, 31, 31, 1.0f);
    Colonist *settler = game_colonist(&game, game_spawn_colonist(&game, 32, 30, 0));
    Animal *deer = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 30, 30 + def->flight_distance + 6);
    REQUIRE(settler != NULL && deer != NULL);
    deer->needs[ANIMAL_NEED_FOOD] = 0.3f;
    for (int i = 0; i < 200; i++) {
        game_tick(&game);
        hold(settler, 32, 30);
    }
    CHECK(deer->task != ANIMAL_TASK_EAT);
    CHECK_EQ_INT(deer->search_failures[ANIMAL_SEARCH_FOOD], 1);
    /* The settler gone, it comes and eats. */
    game_remove_colonist(&game, game_colonist_id_at(&game, 0));
    deer->search_cooldown[ANIMAL_SEARCH_FOOD] = 0;
    for (int i = 0; i < 2000 && deer->task != ANIMAL_TASK_EAT; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_EAT);
    game_shutdown(&game);
}

static void test_followers_stay_with_their_leader(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animals *animals = &game.animals;
    Animal *leader = spawn(&game, ANIMAL_BOAR, ANIMAL_FEMALE, 10, 10);
    Animal *follower = spawn(&game, ANIMAL_BOAR, ANIMAL_FEMALE, 11, 10);
    Animal *loner = spawn(&game, ANIMAL_BOAR, ANIMAL_MALE, 12, 10);
    REQUIRE(leader != NULL && follower != NULL && loner != NULL);
    CHECK(animals_set_leader(animals, id_of(&game, follower), id_of(&game, leader)));
    /* Night: boars are up. The leader is far away, and stays there. */
    game.clock.tick = TODAY + 23 * TICKS_PER_HOUR;
    leader->x = 50;
    leader->y = 50;
    leader->timer = 1000000;
    loner->timer = 1000000;
    for (int i = 0; i < 4000; i++) {
        game_tick(&game);
        leader->needs[ANIMAL_NEED_REST] = 1.0f;
    }
    CHECK(abs(follower->x - 50) <= ANIMAL_HERD_RADIUS + ANIMAL_WANDER_RADIUS);
    CHECK(abs(follower->y - 50) <= ANIMAL_HERD_RADIUS + ANIMAL_WANDER_RADIUS);
    CHECK(loner->x == 12 && loner->y == 10);
    /* It strolls around its leader, never far. */
    for (int i = 0; i < 6000; i++) {
        game_tick(&game);
        leader->needs[ANIMAL_NEED_REST] = 1.0f;
        follower->needs[ANIMAL_NEED_REST] = 1.0f;
        CHECK(abs(follower->x - 50) <= ANIMAL_WANDER_RADIUS + 1);
        CHECK(abs(follower->y - 50) <= ANIMAL_WANDER_RADIUS + 1);
    }
    game_shutdown(&game);
}

static void test_wandering_stays_on_the_ground(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* A fence with a door in it: an animal never gets through. */
    for (int y = 0; y < game.map.height; y++) {
        map_set_structure(&game.map, 32, y, 0, y == 20 ? STRUCTURE_DOOR : STRUCTURE_WALL);
    }
    Animal *hares[6];
    for (int i = 0; i < 6; i++) {
        hares[i] = spawn(&game, ANIMAL_HARE, ANIMAL_FEMALE, 29 + i % 3, 18 + i);
        REQUIRE(hares[i] != NULL);
    }
    game.clock.tick = TODAY + 20 * TICKS_PER_HOUR; /* Evening: hares are up. */
    bool moved = false;
    for (int i = 0; i < 8000; i++) {
        game_tick(&game);
        for (int h = 0; h < 6; h++) {
            hares[h]->needs[ANIMAL_NEED_REST] = 1.0f;
            moved |= animal_is_moving(hares[h]);
            CHECK(hares[h]->x < 32 && hares[h]->z == 0);
            CHECK(map_is_passable(&game.map, hares[h]->x, hares[h]->y, hares[h]->z));
        }
    }
    CHECK(moved);
    game_shutdown(&game);
}

static bool init_wild_game(Game *game, uint64_t seed, float wildlife, int colonists)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = 96;
    params.world.height = 96;
    params.colonist_count = colonists;
    params.wildlife = wildlife;
    return game_init(game, &params);
}

static void test_populated_world(void)
{
    Game game;
    REQUIRE(init_wild_game(&game, 42, 0.0f, 3));
    CHECK_EQ_INT(game.animals.pool.count, 0); /* None unless asked for. */
    game_shutdown(&game);

    REQUIRE(init_wild_game(&game, 42, GAME_WILDLIFE_DENSITY, 3));
    const Animals *animals = &game.animals;
    const Map *map = &game.map;
    int counts[ANIMAL_SPECIES_COUNT] = {0};
    int males[ANIMAL_SPECIES_COUNT] = {0};
    REQUIRE(game.has_arrival);
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        EntityId id = animals_id_at(animals, slot);
        REQUIRE(!entity_id_is_none(id));
        const Animal *animal = &animals->list[slot];
        const AnimalDef *def = animal_def((AnimalSpecies)animal->species);
        counts[animal->species]++;
        males[animal->species] += animal->sex == ANIMAL_MALE ? 1 : 0;
        /* On dry ground it can stand on, away from where the settlers arrive. */
        CHECK(path_walker_can_stand(PATH_WALKER_ANIMAL, map, animal->x, animal->y, animal->z));
        CHECK(!map_is_water(map, animal->x, animal->y, animal->z));
        CHECK(distance_between(animal, game.arrival.x, game.arrival.y) >=
              (float)ANIMAL_SPAWN_CLEARANCE - 12.0f);
        /* Herds of one species and one sex, of the size of their kind. */
        EntityId head = animals_leader(animals, id);
        const Animal *leader = animals_get_const(animals, head);
        REQUIRE(leader != NULL);
        CHECK_EQ_INT(leader->species, animal->species);
        CHECK_EQ_INT(leader->sex, animal->sex);
        int size = animals_herd_size(animals, id);
        CHECK(size >= 1 &&
              size <= (animal->sex == ANIMAL_MALE ? def->male_group_max : def->herd_max));
        CHECK(abs(leader->x - animal->x) <= 12 && abs(leader->y - animal->y) <= 12);
    }
    for (int species = 0; species < ANIMAL_SPECIES_COUNT; species++) {
        /* (A small map may hold none of the carnivores, which are few.) */
        if (!animal_def((AnimalSpecies)species)->carnivore) {
            CHECK(counts[species] >= 2);
            CHECK(males[species] < counts[species]);
        }
    }
    /* Each animal on a tile of its own. */
    for (uint32_t a = 0; a < animals_slot_count(animals); a++) {
        for (uint32_t b = a + 1; b < animals_slot_count(animals); b++) {
            CHECK(animals->list[a].x != animals->list[b].x ||
                  animals->list[a].y != animals->list[b].y ||
                  animals->list[a].z != animals->list[b].z);
        }
    }
    uint32_t base = animals->pool.count;
    game_shutdown(&game);

    /* A big world holds the carnivores too: packs of wolves, foxes alone. */
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 42;
    params.world.width = 256;
    params.world.height = 256;
    params.colonist_count = 3;
    params.wildlife = GAME_WILDLIFE_DENSITY;
    REQUIRE(game_init(&game, &params));
    int wolves = 0, foxes = 0, pack = 0;
    for (uint32_t slot = 0; slot < animals_slot_count(&game.animals); slot++) {
        const Animal *animal = &game.animals.list[slot];
        wolves += animal->species == ANIMAL_WOLF;
        foxes += animal->species == ANIMAL_FOX;
        if (animal->species == ANIMAL_WOLF && animal->sex == ANIMAL_FEMALE) {
            int size = animals_herd_size(&game.animals, animals_id_at(&game.animals, slot));
            pack = size > pack ? size : pack;
        }
    }
    CHECK(wolves >= 3 && foxes >= 3);
    CHECK(pack >= animal_def(ANIMAL_WOLF)->herd_min);
    game_shutdown(&game);

    /* Twice the density, about twice the animals. */
    REQUIRE(init_wild_game(&game, 42, 2.0f * GAME_WILDLIFE_DENSITY, 3));
    CHECK(game.animals.pool.count > base * 3 / 2 && game.animals.pool.count <= base * 5 / 2);
    game_shutdown(&game);
}

static void test_wild_games_are_deterministic(void)
{
    static Game a, b, none;
    REQUIRE(init_wild_game(&a, 7, GAME_WILDLIFE_DENSITY, 3));
    REQUIRE(init_wild_game(&b, 7, GAME_WILDLIFE_DENSITY, 3));
    REQUIRE(init_wild_game(&none, 7, 0.0f, 3));
    CHECK(game_hash(&a) == game_hash(&b));
    CHECK(game_hash(&a) != game_hash(&none));
    for (int i = 0; i < 6000; i++) {
        game_tick(&a);
        game_tick(&b);
    }
    CHECK(game_hash(&a) == game_hash(&b));
    /* They lived: some moved, and every need is where it may be. */
    int moved = 0;
    Game start;
    REQUIRE(init_wild_game(&start, 7, GAME_WILDLIFE_DENSITY, 3));
    for (uint32_t slot = 0; slot < animals_slot_count(&a.animals); slot++) {
        const Animal *animal = &a.animals.list[slot];
        const Animal *before = &start.animals.list[slot];
        moved += animal->x != before->x || animal->y != before->y ? 1 : 0;
        for (int need = 0; need < ANIMAL_NEED_COUNT; need++) {
            CHECK(animal->needs[need] >= 0.0f && animal->needs[need] <= 1.0f);
        }
        CHECK(path_walker_can_stand(PATH_WALKER_ANIMAL, &a.map, animal->x, animal->y, animal->z) ||
              animal_is_moving(animal));
    }
    CHECK(moved > 0);
    /* The pathfinder is handed back to the settlers. */
    CHECK_EQ_INT(a.pathfinder.walker, PATH_WALKER_SETTLER);
    game_shutdown(&start);
    game_shutdown(&a);
    game_shutdown(&b);
    game_shutdown(&none);
}

static void test_seen_and_picked(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *deer = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_MALE, 55, 55);
    REQUIRE(deer != NULL);
    deer->timer = 1000000;
    EntityId id = id_of(&game, deer);
    /* Without the fog, everything is seen. */
    CHECK(game_animal_seen(&game, id));
    CHECK(!game_animal_seen(&game, ENTITY_NONE));
    CHECK(entity_id_equal(game_animal_at(&game, 55.5f, 55.5f, 0), id));
    CHECK(entity_id_is_none(game_animal_at(&game, 50.5f, 55.5f, 0)));
    CHECK(game_animal_const(&game, id) == deer);

    /* Under the fog, only what a settler sees: behind a wall, nobody knows
     * the deer is there. */
    for (int i = 40; i < 64; i++) {
        map_set_structure(&game.map, 40, i, 0, STRUCTURE_WALL);
        map_set_structure(&game.map, i, 40, 0, STRUCTURE_WALL);
    }
    Colonist *settler = game_colonist(&game, game_spawn_colonist(&game, 10, 10, 0));
    REQUIRE(settler != NULL);
    REQUIRE(game_set_fog(&game, true));
    for (int i = 0; i < 400; i++) {
        game_tick(&game);
        hold(settler, 10, 10);
    }
    CHECK(deer->x == 55 && deer->y == 55);
    CHECK(!game_animal_seen(&game, id));
    CHECK(entity_id_is_none(game_animal_at(&game, 55.5f, 55.5f, 0)));
    /* In the open, in view. */
    Animal *hare = spawn(&game, ANIMAL_HARE, ANIMAL_FEMALE, 10, 30);
    REQUIRE(hare != NULL);
    hare->timer = 1000000;
    for (int i = 0; i < 400; i++) {
        game_tick(&game);
        hold(settler, 10, 10);
        hare->x = 10; /* It would run off: held there. */
        hare->y = 30;
        hare->path.count = 0;
        hare->move_progress = 0.0f;
    }
    CHECK(game_animal_seen(&game, id_of(&game, hare)));
    CHECK(entity_id_equal(game_animal_at(&game, 10.5f, 30.5f, 0), id_of(&game, hare)));
    game_shutdown(&game);
}

static void test_debug_spawn_joins_a_herd(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    const Animals *animals = &game.animals;
    EntityId hind = debug_spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 20, 20, 0);
    REQUIRE(!entity_id_is_none(hind));
    CHECK_EQ_INT(animals_herd_size(animals, hind), 1);
    /* Close by, of its species and sex: its herd. */
    EntityId second = debug_spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 23, 21, 0);
    CHECK(entity_id_equal(animals_leader(animals, second), hind));
    EntityId third = debug_spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 26, 22, 0);
    CHECK(entity_id_equal(animals_leader(animals, third), hind)); /* Through the second. */
    CHECK_EQ_INT(animals_herd_size(animals, hind), 3);
    /* A stag, a boar, a hind far away: on their own. */
    EntityId stag = debug_spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_MALE, 21, 20, 0);
    EntityId boar = debug_spawn_ordinary_animal(&game, ANIMAL_BOAR, ANIMAL_FEMALE, 20, 21, 0);
    EntityId far = debug_spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 50, 50, 0);
    CHECK_EQ_INT(animals_herd_size(animals, stag), 1);
    CHECK_EQ_INT(animals_herd_size(animals, boar), 1);
    CHECK_EQ_INT(animals_herd_size(animals, far), 1);
    CHECK_EQ_INT(animals_herd_size(animals, hind), 3);
    CHECK(
        entity_id_is_none(debug_spawn_ordinary_animal(&game, ANIMAL_BOAR, ANIMAL_MALE, -3, 0, 0)));
    game_shutdown(&game);
}

/* An animal faces where its step leads; standing, the side it last turned to. */
static void test_heading(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *boar = spawn(&game, ANIMAL_BOAR, ANIMAL_MALE, 20, 20);
    REQUIRE(boar != NULL);
    boar->facing_left = true;
    CHECK_EQ_INT(animal_heading(boar), HEADING_WEST);
    boar->facing_left = false;
    CHECK_EQ_INT(animal_heading(boar), HEADING_EAST);
    /* A step of its path: owned by the animal, freed with it. */
    path_free(&boar->path);
    boar->path.steps = malloc(sizeof(TilePos));
    REQUIRE(boar->path.steps != NULL);
    boar->path.count = boar->path.capacity = 1;
    boar->path_index = 0;
    boar->path.steps[0] = (TilePos){20, 21, 0};
    CHECK_EQ_INT(animal_heading(boar), HEADING_SOUTH);
    boar->path.steps[0] = (TilePos){20, 19, 0};
    CHECK_EQ_INT(animal_heading(boar), HEADING_NORTH);
    boar->path.steps[0] = (TilePos){19, 19, 0};
    CHECK_EQ_INT(animal_heading(boar), HEADING_WEST);
    boar->path.steps[0] = (TilePos){21, 20, 0};
    CHECK_EQ_INT(animal_heading(boar), HEADING_EAST);
    game_shutdown(&game);
}

/* ---- Characters ----------------------------------------------------------- */

/* Animals have attributes like settlers: they neither learn nor feel the
 * weight of their thoughts (learning and temperament at the very bottom), and
 * differ from one another in the rest. */
static void test_animals_have_characters(void)
{
    Rng rng;
    rng_seed(&rng, 7, 3);
    Attributes first, second;
    attributes_roll_animal(&first, &rng);
    attributes_roll_animal(&second, &rng);
    for (int i = 0; i < 2; i++) {
        const Attributes *attributes = i == 0 ? &first : &second;
        CHECK_EQ_INT(attributes->position[ATTRIBUTE_LEARNING], -ATTRIBUTE_POSITION_MAX);
        CHECK_EQ_INT(attributes->position[ATTRIBUTE_TEMPERAMENT], -ATTRIBUTE_POSITION_MAX);
        CHECK_NEAR(attribute_factor(attributes, ATTRIBUTE_LEARNING), 0.5, 1e-5); /* -100 % */
    }
    CHECK(memcmp(&first, &second, sizeof(first)) != 0);
    /* The same draws give the same character. */
    Rng again;
    rng_seed(&again, 7, 3);
    Attributes repeat;
    attributes_roll_animal(&repeat, &again);
    CHECK(memcmp(&first, &repeat, sizeof(first)) == 0);

    /* In a game: each animal is born with one, and no two are alike. */
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *deer[6];
    for (int i = 0; i < 6; i++) {
        EntityId id = game_spawn_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 10 + 2 * i, 10, 0);
        deer[i] = animals_get(&game.animals, id);
        REQUIRE(deer[i] != NULL);
        CHECK_EQ_INT(deer[i]->attributes.position[ATTRIBUTE_LEARNING], -ATTRIBUTE_POSITION_MAX);
        CHECK_NEAR(deer[i]->health.damage_factor, attribute_damage_factor(&deer[i]->attributes),
                   1e-6);
        CHECK_NEAR(deer[i]->health.pain_factor, attribute_pain_factor(&deer[i]->attributes), 1e-6);
    }
    int different = 0;
    for (int i = 1; i < 6; i++) {
        different += memcmp(&deer[0]->attributes, &deer[i]->attributes, sizeof(Attributes)) != 0;
    }
    CHECK_EQ_INT(different, 5);
    CHECK(deer[0]->attributes.position[ATTRIBUTE_WALKING] !=
              deer[1]->attributes.position[ATTRIBUTE_WALKING] ||
          deer[0]->attributes.position[ATTRIBUTE_RUNNING] !=
              deer[1]->attributes.position[ATTRIBUTE_RUNNING]);
    game_shutdown(&game);
}

/* A dazed animal (lowered consciousness) runs out of breath sooner and
 * notices a settler later. */
static void test_dazed_animal(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    const AnimalDef *def = animal_def(ANIMAL_ROE_DEER);
    Animal *alert = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 30, 20);
    Animal *dazed = spawn(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 30, 40);
    REQUIRE(alert != NULL && dazed != NULL);
    dazed->health.blood = 0.85f - 0.1225f; /* 65 % consciousness: vigor 0.69. */
    health_update(&dazed->health, 1e-6f);
    CHECK_NEAR(animal_sprint_seconds(dazed), animal_sprint_seconds(alert) * 0.6875, 1e-3);
    alert->timer = dazed->timer = 100000; /* Standing there. */
    run(&game, 2 * ANIMAL_ALERT_TICKS);
    /* A settler at 80 % of the flight distance from each. */
    int away = def->flight_distance * 8 / 10;
    Colonist *near_alert = game_colonist(&game, game_spawn_colonist(&game, 30 - away, 20, 0));
    Colonist *near_dazed = game_colonist(&game, game_spawn_colonist(&game, 30 - away, 40, 0));
    REQUIRE(near_alert != NULL && near_dazed != NULL);
    bool alert_fled = false, dazed_fled = false;
    for (int i = 0; i < 2 * ANIMAL_ALERT_TICKS; i++) {
        game_tick(&game);
        alert_fled |= alert->task == ANIMAL_TASK_FLEE;
        dazed_fled |= dazed->task == ANIMAL_TASK_FLEE;
        hold(near_alert, 30 - away, 20);
        hold(near_dazed, 30 - away, 40);
    }
    CHECK(alert_fled);
    CHECK(!dazed_fled);
    game_shutdown(&game);
}

/* The species' values are the ordinary ones: the attributes move them. */
static void test_attributes_change_the_species_values(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *wolf = spawn(&game, ANIMAL_WOLF, ANIMAL_MALE, 10, 10);
    REQUIRE(wolf != NULL);
    const AnimalDef *def = animal_def(ANIMAL_WOLF);
    CHECK_NEAR(animal_walk_speed(wolf), def->walk_speed, 1e-6);
    CHECK_NEAR(animal_run_speed(wolf), def->flee_speed, 1e-6);
    CHECK_NEAR(animal_sprint_seconds(wolf), def->sprint_seconds, 1e-6);
    CHECK_NEAR(animal_nerve(wolf), def->nerve, 1e-6);
    CHECK_NEAR(animal_cold_limit(wolf), def->cold_below, 1e-6);
    CHECK_NEAR(animal_heat_limit(wolf), def->heat_above, 1e-6);

    Attributes extremes;
    attributes_init(&extremes);
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_WALKING, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_RUNNING, -ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_ENDURANCE, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_COURAGE, -ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_COLD_TOLERANCE, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_HEAT_TOLERANCE, -ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_TOUGHNESS, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_PAIN_TOLERANCE, -ATTRIBUTE_POSITION_MAX));
    animal_set_attributes(wolf, &extremes);
    CHECK_NEAR(animal_walk_speed(wolf), 2.0 * def->walk_speed, 1e-5);
    CHECK_NEAR(animal_run_speed(wolf), 0.5 * def->flee_speed, 1e-5);
    CHECK_NEAR(animal_sprint_seconds(wolf), 2.0 * def->sprint_seconds, 1e-4);
    CHECK_NEAR(animal_nerve(wolf), def->nerve * 0.08 / 0.4, 1e-5); /* The most fearful. */
    CHECK_NEAR(animal_cold_limit(wolf), def->cold_below - 5.0, 1e-5);
    CHECK_NEAR(animal_heat_limit(wolf), def->heat_above - 5.0, 1e-5);
    CHECK_NEAR(wolf->health.damage_factor, 0.5, 1e-5);
    CHECK_NEAR(wolf->health.pain_factor, 2.0, 1e-5);
    /* The very bravest never break off. */
    REQUIRE(attributes_set(&extremes, ATTRIBUTE_COURAGE, ATTRIBUTE_POSITION_MAX));
    animal_set_attributes(wolf, &extremes);
    CHECK(animal_nerve(wolf) > 1.0f);
    game_shutdown(&game);
}

/* A big eater gets hungry sooner, a sound sleeper rests faster. */
static void test_appetite_and_sleep_of_animals(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *glutton = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 10, 10);
    Animal *picker = spawn(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 40, 40);
    REQUIRE(glutton != NULL && picker != NULL);
    Attributes big, small;
    attributes_init(&big);
    attributes_init(&small);
    REQUIRE(attributes_set(&big, ATTRIBUTE_APPETITE, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&small, ATTRIBUTE_APPETITE, -ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&big, ATTRIBUTE_SLEEP, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&small, ATTRIBUTE_SLEEP, -ATTRIBUTE_POSITION_MAX));
    animal_set_attributes(glutton, &big);
    animal_set_attributes(picker, &small);
    for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
        glutton->needs[ANIMAL_NEED_WATER] = picker->needs[ANIMAL_NEED_WATER] = 1.0f;
        glutton->needs[ANIMAL_NEED_REST] = picker->needs[ANIMAL_NEED_REST] = 1.0f;
        glutton->timer = picker->timer = 100000;
        game_tick(&game);
    }
    float lost_big = 1.0f - glutton->needs[ANIMAL_NEED_FOOD];
    float lost_small = 1.0f - picker->needs[ANIMAL_NEED_FOOD];
    CHECK(lost_big > 3.5f * lost_small); /* x2 against x0.5. */
    game_shutdown(&game);
}

/* A quick runner reaches its prey first; one with more endurance tires later. */
static void test_quick_animals_catch_up_sooner(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId quick_id, slow_id, hare_a, hare_b;
    Animal *quick =
        animals_get(&game.animals,
                    (quick_id = spawn_ordinary_animal(&game, ANIMAL_BOAR, ANIMAL_MALE, 10, 10, 0)));
    Animal *slow =
        animals_get(&game.animals,
                    (slow_id = spawn_ordinary_animal(&game, ANIMAL_BOAR, ANIMAL_MALE, 10, 30, 0)));
    Animal *prey_a =
        animals_get(&game.animals,
                    (hare_a = spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 30, 10, 0)));
    Animal *prey_b =
        animals_get(&game.animals,
                    (hare_b = spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 30, 30, 0)));
    REQUIRE(quick && slow && prey_a && prey_b);
    for (int part = 0; part < body_part_count(BODY_HARE); part++) {
        if (body_part_def(BODY_HARE, part)->role == BODY_ROLE_WALK) {
            health_hurt(&prey_a->health, part, INJURY_FRACTURE, 0.5f);
            health_hurt(&prey_b->health, part, INJURY_FRACTURE, 0.5f);
        }
    }
    Attributes fast, sluggish;
    attributes_init(&fast);
    attributes_init(&sluggish);
    REQUIRE(attributes_set(&fast, ATTRIBUTE_RUNNING, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&sluggish, ATTRIBUTE_RUNNING, -ATTRIBUTE_POSITION_MAX));
    animal_set_attributes(quick, &fast);
    animal_set_attributes(slow, &sluggish);
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, quick_id},
                              (Combatant){COMBATANT_ANIMAL, hare_a}));
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, slow_id},
                              (Combatant){COMBATANT_ANIMAL, hare_b}));
    int quick_at = -1, slow_at = -1;
    for (int tick = 0; tick < 12 * TICKS_PER_SECOND && (quick_at < 0 || slow_at < 0); tick++) {
        game_tick(&game);
        if (quick_at < 0 && abs(quick->x - 30) <= 1) {
            quick_at = tick;
        }
        if (slow_at < 0 && abs(slow->x - 30) <= 1) {
            slow_at = tick;
        }
    }
    REQUIRE(quick_at > 0 && slow_at > 0);
    CHECK(slow_at > 3 * quick_at / 2); /* x2 against x0.5: about four times the time. */
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_species_definitions);
    RUN_TEST(test_spawn_and_remove);
    RUN_TEST(test_herds);
    RUN_TEST(test_needs_go_down);
    RUN_TEST(test_grazing);
    RUN_TEST(test_treats_before_grass);
    RUN_TEST(test_roots_nuts_and_mushrooms);
    RUN_TEST(test_drinking);
    RUN_TEST(test_sleep);
    RUN_TEST(test_sight);
    RUN_TEST(test_flees_settlers);
    RUN_TEST(test_herd_runs_together);
    RUN_TEST(test_cornered);
    RUN_TEST(test_keeps_away_from_settlers);
    RUN_TEST(test_followers_stay_with_their_leader);
    RUN_TEST(test_wandering_stays_on_the_ground);
    RUN_TEST(test_populated_world);
    RUN_TEST(test_wild_games_are_deterministic);
    RUN_TEST(test_seen_and_picked);
    RUN_TEST(test_debug_spawn_joins_a_herd);
    RUN_TEST(test_heading);
    RUN_TEST(test_animals_have_characters);
    RUN_TEST(test_dazed_animal);
    RUN_TEST(test_animals_eat_the_wild_fruit);
    RUN_TEST(test_attributes_change_the_species_values);
    RUN_TEST(test_appetite_and_sleep_of_animals);
    RUN_TEST(test_quick_animals_catch_up_sooner);
    return test_summary();
}
