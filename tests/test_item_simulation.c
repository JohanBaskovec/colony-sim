/*
 * Long runs of whole colonies (core/scenario.h) with the items checked every
 * in-game hour (game_check_items()): gathering, building, cooking, with the
 * fog of war and with wild animals. Items are dropped, hauled, piled in
 * heaps of several kinds, picked from under others, eaten, rotted, burnt
 * and built in all along.
 */
#include "core/game.h"
#include "core/scenario.h"

#include "test_framework.h"

/* A colony of `colonists` on a `size` map, the scenario set up. */
static bool init_colony(Game *game, int size, int colonists, Scenario scenario, float wildlife)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 42;
    params.world.width = size;
    params.world.height = size;
    params.colonist_count = colonists;
    params.wildlife = wildlife;
    return game_init(game, &params) && scenario_apply(game, scenario);
}

/* Runs `hours` in-game hours, checking the items after each; stops at the
 * first problem. Returns the hours run without one. */
static int run_checked(Game *game, int hours)
{
    for (int hour = 0; hour < hours; hour++) {
        for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
            game_tick(game);
        }
        ItemsCheck check = game_check_items(game);
        if (check.problems > 0) {
            fprintf(stderr, "hour %d: %d problems, first: %s (tile %ld)\n", hour, check.problems,
                    check.first, check.first_tile);
            return hour;
        }
    }
    return hours;
}

/* Crafts done by every bill. */
static int crafts_done(const Game *game)
{
    int done = 0;
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = workstation_get_const(&game->workstations, index);
        for (int bill = 0; station != NULL && bill < station->bill_count; bill++) {
            done += station->bills[bill].done;
        }
    }
    return done;
}

/* Items in stockpiles, of any kind. */
static long stored_items(const Game *game)
{
    long stored[ITEM_TYPE_COUNT] = {0};
    stockpiles_count_stored(&game->stockpiles, &game->items, stored);
    long total = 0;
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        total += stored[type];
    }
    return total;
}

static void test_gathering(void)
{
    static Game game;
    REQUIRE(init_colony(&game, 96, 8, SCENARIO_GATHERING, 0.0f));
    CHECK_EQ_INT(run_checked(&game, 3 * HOURS_PER_DAY), 3 * HOURS_PER_DAY);
    CHECK(stored_items(&game) > 0);            /* Hauled. */
    CHECK(items_heaps(&game.items).mixed > 0); /* Heaps of several kinds formed. */
    game_shutdown(&game);
}

static void test_building(void)
{
    static Game game;
    REQUIRE(init_colony(&game, 96, 8, SCENARIO_CONSTRUCTION, 0.0f));
    int sites = game.constructions.used_count;
    /* Six days: the grass of the walls dries for three. */
    CHECK_EQ_INT(run_checked(&game, 6 * HOURS_PER_DAY), 6 * HOURS_PER_DAY);
    CHECK(game.constructions.used_count < sites); /* Built. */
    game_shutdown(&game);
}

static void test_cooking(void)
{
    static Game game;
    REQUIRE(init_colony(&game, 96, 8, SCENARIO_COOKING, 0.0f));
    CHECK_EQ_INT(run_checked(&game, 2 * HOURS_PER_DAY), 2 * HOURS_PER_DAY);
    CHECK(crafts_done(&game) > 0);
    CHECK(game.items.totals[ITEM_ROASTED_CARROTS] + game.items.totals[ITEM_ROASTED_RHIZOMES] +
              game.items.rotted[ITEM_ROASTED_CARROTS] + game.items.rotted[ITEM_ROASTED_RHIZOMES] >
          0);
    game_shutdown(&game);
}

static void test_gathering_in_the_fog(void)
{
    static Game game;
    REQUIRE(init_colony(&game, 96, 8, SCENARIO_GATHERING, 0.0f));
    REQUIRE(game_set_fog(&game, true));
    CHECK_EQ_INT(run_checked(&game, 2 * HOURS_PER_DAY), 2 * HOURS_PER_DAY);
    game_shutdown(&game);
}

static void test_gathering_among_wild_animals(void)
{
    static Game game;
    REQUIRE(init_colony(&game, 96, 8, SCENARIO_GATHERING, 3.0f * GAME_WILDLIFE_DENSITY));
    CHECK_EQ_INT(run_checked(&game, 2 * HOURS_PER_DAY), 2 * HOURS_PER_DAY);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_gathering);
    RUN_TEST(test_building);
    RUN_TEST(test_cooking);
    RUN_TEST(test_gathering_in_the_fog);
    RUN_TEST(test_gathering_among_wild_animals);
    return test_summary();
}
