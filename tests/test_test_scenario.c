/* Test scenarios of the F2 debug menu (core/test_scenario.h). */
#include "core/i18n.h"
#include "core/test_scenario.h"

#include "test_framework.h"

#include <stdlib.h>

static int count_species(const Game *game, AnimalSpecies species)
{
    int count = 0;
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        const Animal *animal = &game->animals.list[slot];
        count +=
            !entity_id_is_none(animals_id_at(&game->animals, slot)) && animal->species == species;
    }
    return count;
}

static Game *new_game(void)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 21;
    params.colonist_count = 3;
    params.world.width = 64;
    params.world.height = 64;
    return game_init(&game, &params) ? &game : NULL;
}

static void test_names(void)
{
    for (int i = 0; i < TEST_SCENARIO_COUNT; i++) {
        const char *key = test_scenario_name_key((TestScenario)i);
        REQUIRE(key != NULL);
        CHECK(i18n_is_valid_key(key));
    }
    CHECK(test_scenario_name_key(TEST_SCENARIO_COUNT) == NULL);
}

/* The world is flat and bare; the game it replaces is gone. */
static void check_bare_world(const Game *game)
{
    const Map *map = &game->map;
    CHECK_EQ_INT(map->width, TEST_SCENARIO_WORLD_SIZE);
    CHECK_EQ_INT(map->height, TEST_SCENARIO_WORLD_SIZE);
    for (int y = 0; y < map->height; y += 7) {
        for (int x = 0; x < map->width; x += 5) {
            size_t i = map_index(map, x, y, 0);
            CHECK_EQ_INT(map->terrain[i], TERRAIN_SOIL);
            CHECK_EQ_INT(map->rock[i], ROCK_NONE);
            CHECK_EQ_INT(map->plants[i].type, PLANT_NONE);
            CHECK(map_is_passable(map, x, y, 0));
        }
    }
    CHECK(!game->fog_on);
}

static void test_wolves_and_deer(void)
{
    Game *game = new_game();
    REQUIRE(game != NULL);
    game_set_instant_build(game, true);
    game_clock_set_speed(&game->clock, GAME_SPEED_FAST);
    uint64_t seed = game->seed;
    REQUIRE(test_scenario_load(game, TEST_SCENARIO_WOLVES_AND_DEER));
    check_bare_world(game);
    CHECK_EQ_INT((int)game->seed, (int)seed);
    CHECK_EQ_INT((int)game->colonist_pool.count, 0);
    CHECK_EQ_INT(count_species(game, ANIMAL_WOLF), TEST_SCENARIO_WOLF_COUNT);
    CHECK_EQ_INT(count_species(game, ANIMAL_ROE_DEER), TEST_SCENARIO_DEER_COUNT);
    CHECK_EQ_INT((int)game->animals.pool.count,
                 TEST_SCENARIO_WOLF_COUNT + TEST_SCENARIO_DEER_COUNT);
    /* Debug flags and clock speed are kept. */
    CHECK(game->debug.instant_build);
    CHECK_EQ_INT(game->clock.speed, GAME_SPEED_FAST);
    int wolf_x = 0, deer_x = 0;
    EntityId first_wolf = ENTITY_NONE, first_deer = ENTITY_NONE;
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        EntityId id = animals_id_at(&game->animals, slot);
        const Animal *animal = &game->animals.list[slot];
        if (entity_id_is_none(id)) {
            continue;
        }
        if (animal->species == ANIMAL_WOLF) {
            CHECK_NEAR(animal->needs[ANIMAL_NEED_FOOD], TEST_SCENARIO_WOLF_FOOD, 1e-6f);
            wolf_x += animal->x;
            first_wolf = entity_id_is_none(first_wolf) ? id : first_wolf;
        } else {
            deer_x += animal->x;
            first_deer = entity_id_is_none(first_deer) ? id : first_deer;
        }
    }
    /* One pack and one herd, a few tiles apart. */
    CHECK_EQ_INT(animals_herd_size(&game->animals, first_wolf), TEST_SCENARIO_WOLF_COUNT);
    CHECK_EQ_INT(animals_herd_size(&game->animals, first_deer), TEST_SCENARIO_DEER_COUNT);
    int distance = wolf_x / TEST_SCENARIO_WOLF_COUNT - deer_x / TEST_SCENARIO_DEER_COUNT;
    CHECK(abs(distance - TEST_SCENARIO_DISTANCE) <= 1);
    /* The hungry wolves hunt: the deer are chased and, with time, one is caught. */
    int bites = 0;
    for (int t = 0; t < 120 * TICKS_PER_SECOND && bites == 0; t++) {
        game_tick(game);
        bites = (int)game->strikes_resolved;
    }
    CHECK(bites > 0);
    game_shutdown(game);
}

static void test_wolves_and_settlers(void)
{
    Game *game = new_game();
    REQUIRE(game != NULL);
    REQUIRE(test_scenario_load(game, TEST_SCENARIO_WOLVES_AND_SETTLERS));
    check_bare_world(game);
    CHECK_EQ_INT(count_species(game, ANIMAL_WOLF), TEST_SCENARIO_WOLF_COUNT);
    CHECK_EQ_INT((int)game->animals.pool.count, TEST_SCENARIO_WOLF_COUNT);
    REQUIRE(game->colonist_pool.count == 2);
    int armed = 0, bare = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *settler = game_colonist_const(game, game_colonist_id_at(game, slot));
        REQUIRE(settler != NULL);
        float skill, power;
        AttackType attack = colonist_attack(settler, &skill, &power);
        armed += attack == ATTACK_SPEAR_THRUST;
        bare += attack == ATTACK_FISTS;
        CHECK(abs(settler->x - (TEST_SCENARIO_WORLD_SIZE / 2 - TEST_SCENARIO_DISTANCE / 2)) <= 1);
    }
    CHECK_EQ_INT(armed, 1);
    CHECK_EQ_INT(bare, 1);
    /* Nobody is told they rediscover what they knew at the start. */
    CHECK_EQ_INT((int)game->notices.count, 0);
    /* The wolves are hungry enough to go for settlers: someone is bitten. */
    for (int t = 0; t < 120 * TICKS_PER_SECOND && game->strikes_resolved == 0; t++) {
        game_tick(game);
    }
    CHECK(game->strikes_resolved > 0);
    /* The pack fights on around them: no wolf unhurt runs off (one with no
     * room beside the settlers waits, or goes round to a free tile). */
    bool unhurt_fled = false;
    for (int t = 0; t < 20 * TICKS_PER_SECOND; t++) {
        game_tick(game);
        for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
            const Animal *wolf = &game->animals.list[slot];
            unhurt_fled |= !entity_id_is_none(animals_id_at(&game->animals, slot)) &&
                           wolf->task == ANIMAL_TASK_FLEE && !health_is_hurt(&wolf->health);
        }
    }
    CHECK(!unhurt_fled);
    game_shutdown(game);
}

/* The feud: eight settlers in two clans, food beside them, no animals. */
static void test_feud(void)
{
    Game *game = new_game();
    REQUIRE(game != NULL);
    REQUIRE(test_scenario_load(game, TEST_SCENARIO_FEUD));
    check_bare_world(game);
    CHECK_EQ_INT((int)game->animals.pool.count, 0);
    REQUIRE(game->colonist_pool.count == TEST_SCENARIO_FEUD_SETTLERS);
    int enemies = 0, friends = 0;
    for (int a = 0; a < TEST_SCENARIO_FEUD_SETTLERS; a++) {
        for (int b = 0; b < TEST_SCENARIO_FEUD_SETTLERS; b++) {
            if (a != b) {
                int level = game->social.bonds[a][b].level;
                enemies += level == RELATION_ENEMY;
                friends += level == RELATION_FRIEND;
                CHECK(level == (a % 2 == b % 2 ? RELATION_FRIEND : RELATION_ENEMY));
            }
        }
    }
    CHECK_EQ_INT(enemies, 32);
    CHECK_EQ_INT(friends, 24);
    game_shutdown(game);
}

/* Loading again replaces the last scenario instead of adding to it. */
static void test_reload(void)
{
    Game *game = new_game();
    REQUIRE(game != NULL);
    REQUIRE(test_scenario_load(game, TEST_SCENARIO_WOLVES_AND_DEER));
    for (int t = 0; t < 600; t++) {
        game_tick(game);
    }
    REQUIRE(test_scenario_load(game, TEST_SCENARIO_WOLVES_AND_SETTLERS));
    CHECK_EQ_INT(count_species(game, ANIMAL_ROE_DEER), 0);
    CHECK_EQ_INT(count_species(game, ANIMAL_WOLF), TEST_SCENARIO_WOLF_COUNT);
    CHECK_EQ_INT((int)game->colonist_pool.count, 2);
    CHECK_EQ_INT((int)game->strikes_resolved, 0);
    CHECK_EQ_INT(game->combat_log.count, 0);
    /* Out of range: nothing changes. */
    CHECK(!test_scenario_load(game, TEST_SCENARIO_COUNT));
    CHECK_EQ_INT((int)game->colonist_pool.count, 2);
    game_shutdown(game);
}

int main(void)
{
    RUN_TEST(test_names);
    RUN_TEST(test_wolves_and_deer);
    RUN_TEST(test_wolves_and_settlers);
    RUN_TEST(test_feud);
    RUN_TEST(test_reload);
    return test_summary();
}
