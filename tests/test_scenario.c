/* Test situations set up on a new game (core/scenario.h). */
#include "core/scenario.h"

#include "test_framework.h"

/* `seed` 42 on 200 × 200 tiles: the world of bench_simulation. */
static bool init_game(Game *game, uint64_t seed, int size, int colonists)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = size;
    params.world.height = size;
    params.colonist_count = colonists;
    return game_init(game, &params);
}

static int zone_tiles(const Game *game)
{
    int tiles = 0;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        const GatherZone *zone = gather_zone_get(&game->gather_zones, i);
        tiles += zone != NULL ? zone->cell_count : 0;
    }
    return tiles;
}

static int stockpile_count(const Game *game)
{
    int count = 0;
    for (int i = 0; i < STOCKPILE_MAX; i++) {
        count += stockpile_get(&game->stockpiles, i) != NULL;
    }
    return count;
}

static void test_scenarios(void)
{
    Game game;
    REQUIRE(init_game(&game, 42, 200, 3));
    uint64_t fresh = game_hash(&game);
    CHECK(scenario_apply(&game, SCENARIO_NONE));
    CHECK(game_hash(&game) == fresh);
    CHECK(!scenario_apply(&game, SCENARIO_COUNT));
    CHECK(!scenario_apply(&game, (Scenario)-1));
    CHECK(game_hash(&game) == fresh);
    game_shutdown(&game);

    REQUIRE(init_game(&game, 42, 200, 3));
    CHECK(scenario_apply(&game, SCENARIO_GATHERING));
    CHECK(zone_tiles(&game) > 200 * 200 / 2); /* The land, everywhere. */
    CHECK_EQ_INT(stockpile_count(&game), 2);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    game_shutdown(&game);

    REQUIRE(init_game(&game, 42, 200, 3));
    CHECK(scenario_apply(&game, SCENARIO_UNREACHABLE));
    const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
    REQUIRE(first != NULL);
    /* Nothing of the zone where the colonists walk. */
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, first->x, first->y, first->z),
                 GATHER_ZONE_NONE);
    CHECK_EQ_INT(stockpile_count(&game), 0);
    game_shutdown(&game);

    /* Feuds: gathering, and two clans (friends within, enemies across). */
    REQUIRE(init_game(&game, 42, 200, 3));
    CHECK(scenario_apply(&game, SCENARIO_FEUDS));
    CHECK(zone_tiles(&game) > 200 * 200 / 2);
    CHECK_EQ_INT(game.social.bonds[0][2].level, RELATION_FRIEND);
    CHECK_EQ_INT(game.social.bonds[0][1].level, RELATION_ENEMY);
    CHECK_EQ_INT(game.social.bonds[1][2].level, RELATION_ENEMY);
    game_shutdown(&game);

    REQUIRE(init_game(&game, 42, 200, 3));
    CHECK(scenario_apply(&game, SCENARIO_CONSTRUCTION));
    CHECK(game.constructions.used_count > 0); /* Walls, a screen and beds. */
    CHECK_EQ_INT(stockpile_count(&game), 1);
    CHECK(zone_tiles(&game) > 0);
    game_shutdown(&game);
}

/* The same seed and scenario give the same game, ticks later. */
static void test_deterministic(void)
{
    static Game a, b;
    REQUIRE(init_game(&a, 9, 64, 4));
    REQUIRE(init_game(&b, 9, 64, 4));
    CHECK(scenario_apply(&a, SCENARIO_CONSTRUCTION));
    CHECK(scenario_apply(&b, SCENARIO_CONSTRUCTION));
    for (int i = 0; i < 2000; i++) {
        game_tick(&a);
        game_tick(&b);
    }
    CHECK(game_hash(&a) == game_hash(&b));
    game_shutdown(&a);
    game_shutdown(&b);
}

/* Without colonists, the scenarios built next to the colony do nothing. */
static void test_no_colonists(void)
{
    Game game;
    REQUIRE(init_game(&game, 42, 200, 0));
    CHECK(scenario_apply(&game, SCENARIO_CONSTRUCTION));
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK_EQ_INT(stockpile_count(&game), 0);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_scenarios);
    RUN_TEST(test_deterministic);
    RUN_TEST(test_no_colonists);
    return test_summary();
}
