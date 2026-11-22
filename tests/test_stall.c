/* Stall watch (core/stall.h): settlers stuck on their way. */
#include "core/game.h"
#include "core/stall.h"

#include "test_framework.h"

#include <string.h>

/* A game on a flat plain of soil with no starting colonists, at noon. */
static bool init_plain(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN;
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    game->clock.tick = (uint64_t)GAME_START_DAY * TICKS_PER_DAY + 12 * TICKS_PER_HOUR;
    return true;
}

/* A settler given a path it does not follow (the game is not ticked): stuck
 * after STALL_TICKS on its tile, counted once; then free again once it stands
 * elsewhere, or has nowhere to go. */
static void test_a_path_not_followed_is_a_stall(void)
{
    static Game game;
    REQUIRE(init_plain(&game));
    EntityId id = game_spawn_colonist(&game, 20, 20, 0);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    REQUIRE(pathfinder_find(&game.pathfinder, &game.map, (TilePos){20, 20, 0}, (TilePos){30, 20, 0},
                            100.0f, &colonist->path));
    colonist->path_index = 0;
    REQUIRE(colonist_is_moving(colonist));

    StallWatch watch;
    stall_watch_init(&watch);
    stall_watch_update(&watch, &game); /* Where it stands: the first look. */
    for (int tick = 1; tick < STALL_TICKS; tick++) {
        CHECK_EQ_INT(stall_watch_update(&watch, &game), 0);
    }
    CHECK_EQ_INT(stall_watch_update(&watch, &game), 1);
    CHECK_EQ_INT(stall_watch_update(&watch, &game), 1); /* Still stuck... */
    CHECK_EQ_INT(watch.stalls, 1);                      /* ...counted once. */
    CHECK_EQ_INT((int)watch.longest, STALL_TICKS + 1);
    CHECK_EQ_INT(watch.longest_slot, 0);

    colonist->x = 21; /* A step. */
    CHECK_EQ_INT(stall_watch_update(&watch, &game), 0);
    CHECK_EQ_INT((int)watch.still[0], 0);
    for (int tick = 0; tick < STALL_TICKS; tick++) {
        stall_watch_update(&watch, &game);
    }
    CHECK_EQ_INT(watch.stalls, 2);

    path_clear(&colonist->path); /* Nowhere to go: it may stay. */
    colonist->path_index = 0;
    for (int tick = 0; tick < 2 * STALL_TICKS; tick++) {
        CHECK_EQ_INT(stall_watch_update(&watch, &game), 0);
    }
    CHECK_EQ_INT(watch.stalls, 2);
    game_shutdown(&game);
}

/* Settlers walking where they are sent, then standing about: never stuck. */
static void test_walkers_are_not_stuck(void)
{
    static Game game;
    REQUIRE(init_plain(&game));
    EntityId ids[3];
    for (int i = 0; i < 3; i++) {
        ids[i] = game_spawn_colonist(&game, 5 + i, 5, 0);
        REQUIRE(!entity_id_is_none(ids[i]));
    }
    REQUIRE(game_order_move(&game, ids, 3, 40, 40, 0) == 3);
    StallWatch watch;
    stall_watch_init(&watch);
    bool arrived = false;
    for (int tick = 0; tick < 2 * TICKS_PER_HOUR; tick++) {
        game_tick(&game);
        CHECK_EQ_INT(stall_watch_update(&watch, &game), 0);
        const Colonist *first = game_colonist_const(&game, ids[0]);
        arrived |= first != NULL && first->x > 35 && first->y > 35;
    }
    CHECK_EQ_INT(watch.stalls, 0);
    CHECK(watch.longest < STALL_TICKS);
    CHECK(arrived); /* They did walk there (and about, after). */
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_a_path_not_followed_is_a_stall);
    RUN_TEST(test_walkers_are_not_stuck);
    return test_summary();
}
