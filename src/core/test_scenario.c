#include "core/test_scenario.h"

#include "core/i18n.h"
#include "core/item.h"
#include "core/log.h"

#include <string.h>

static const char *const NAME_KEYS[TEST_SCENARIO_COUNT] = {
    [TEST_SCENARIO_WOLVES_AND_DEER] = N_("debug.scenario.wolves_deer"),
    [TEST_SCENARIO_WOLVES_AND_SETTLERS] = N_("debug.scenario.wolves_settlers"),
};

const char *test_scenario_name_key(TestScenario scenario)
{
    return (unsigned)scenario < TEST_SCENARIO_COUNT ? NAME_KEYS[scenario] : NULL;
}

/* The whole world level is bare soil, open air above it. */
static void flatten(Game *game)
{
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
}

/* Roe does in one herd around (x, y), the first leading. */
static void place_deer(Game *game, int x, int y)
{
    static const int OFFSETS[TEST_SCENARIO_DEER_COUNT][2] = {{0, 0}, {1, 1}, {-1, 1}};
    for (int i = 0; i < TEST_SCENARIO_DEER_COUNT; i++) {
        game_debug_spawn_animal(game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, x + OFFSETS[i][0],
                                y + OFFSETS[i][1], 0);
    }
}

/* A pack of hungry wolves around (x, y), the first leading. */
static void place_wolves(Game *game, int x, int y)
{
    static const int OFFSETS[TEST_SCENARIO_WOLF_COUNT][2] = {{0, 0}, {1, 1}, {-1, 1}, {0, 2}};
    for (int i = 0; i < TEST_SCENARIO_WOLF_COUNT; i++) {
        EntityId id = game_debug_spawn_animal(game, ANIMAL_WOLF, ANIMAL_FEMALE, x + OFFSETS[i][0],
                                              y + OFFSETS[i][1], 0);
        Animal *wolf = animals_get(&game->animals, id);
        if (wolf != NULL) {
            wolf->needs[ANIMAL_NEED_FOOD] = TEST_SCENARIO_WOLF_FOOD;
        }
    }
}

/* A settler on a tile; with a thrusting spear in its tools when `armed`. */
static void place_settler(Game *game, int x, int y, bool armed)
{
    Colonist *settler = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (settler != NULL && armed) {
        colonist_add_tool(settler, item_stack_pristine(ITEM_THRUSTING_SPEAR, 1));
    }
}

bool test_scenario_load(Game *game, TestScenario scenario)
{
    if ((unsigned)scenario >= TEST_SCENARIO_COUNT) {
        return false;
    }
    GameDebug debug = game->debug;
    GameClock clock = game->clock;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = game->seed;
    params.world.flat = true;
    params.world.width = TEST_SCENARIO_WORLD_SIZE;
    params.world.height = TEST_SCENARIO_WORLD_SIZE;
    params.colonist_count = 0;
    game_shutdown(game);
    if (!game_init(game, &params)) {
        LOG_ERROR("Cannot build test scenario %d", (int)scenario);
        return false;
    }
    flatten(game);
    game->debug = debug;
    game->clock.speed = clock.speed;
    game->clock.resume_speed = clock.resume_speed;

    int middle = TEST_SCENARIO_WORLD_SIZE / 2;
    int prey_x = middle - TEST_SCENARIO_DISTANCE / 2;
    int wolves_x = middle + TEST_SCENARIO_DISTANCE / 2;
    game->arrival = (TilePos){(int16_t)middle, (int16_t)middle, 0};
    game->has_arrival = true;
    switch (scenario) {
    case TEST_SCENARIO_WOLVES_AND_DEER: place_deer(game, prey_x, middle); break;
    case TEST_SCENARIO_WOLVES_AND_SETTLERS:
        place_settler(game, prey_x, middle - 1, false);
        place_settler(game, prey_x, middle + 1, true);
        break;
    default: break;
    }
    place_wolves(game, wolves_x, middle);
    game_update_stock(game);
    /* Who knows what now is the baseline: no notice for what the settlers
     * already knew when the scenario was built. */
    game->knowledge_counted = false;
    game_count_knowledge(game);
    return true;
}
