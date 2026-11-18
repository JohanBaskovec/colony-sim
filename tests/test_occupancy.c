#include "core/occupancy.h"

#include "core/game.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

static TilePos at(int x, int y)
{
    return (TilePos){(int16_t)x, (int16_t)y, 0};
}

static void test_one_per_tile(void)
{
    Occupancy occupancy;
    REQUIRE(occupancy_init(&occupancy, 8, 8, 1));
    BeingId a = occupancy_settler(0), b = occupancy_animal(3);
    CHECK(occupancy_claim(&occupancy, a, at(2, 2)));
    CHECK(occupancy_holds(&occupancy, a, at(2, 2)));
    CHECK_EQ_INT(occupancy_holder(&occupancy, at(2, 2)), a);
    CHECK(!occupancy_can_stop(&occupancy, b, at(2, 2)));
    CHECK(!occupancy_claim(&occupancy, b, at(2, 2)));
    CHECK(occupancy_claim(&occupancy, b, at(3, 2)));
    /* A claim moves: the tile before is free again. */
    CHECK(occupancy_claim(&occupancy, a, at(4, 4)));
    CHECK_EQ_INT(occupancy_holder(&occupancy, at(2, 2)), OCCUPANCY_NOBODY);
    CHECK(occupancy_claim(&occupancy, b, at(2, 2)));
    CHECK_EQ_INT(occupancy_holder(&occupancy, at(3, 2)), OCCUPANCY_NOBODY);
    occupancy_release(&occupancy, b);
    CHECK_EQ_INT(occupancy_holder(&occupancy, at(2, 2)), OCCUPANCY_NOBODY);
    /* Out of the map: nowhere to stop. */
    CHECK(!occupancy_claim(&occupancy, a, at(8, 0)));
    CHECK(!occupancy_claim(&occupancy, a, at(-1, 0)));
    occupancy_free(&occupancy);
}

static void test_yielding_holders_make_way(void)
{
    Occupancy occupancy;
    REQUIRE(occupancy_init(&occupancy, 8, 8, 1));
    BeingId idle = occupancy_settler(1), busy = occupancy_settler(2), stroll = occupancy_animal(0);
    occupancy_set_yielding(&occupancy, idle, true);
    occupancy_set_yielding(&occupancy, stroll, true);
    CHECK(occupancy_claim(&occupancy, idle, at(1, 1)));
    /* Another idle one does not take it, a busy one does. */
    CHECK(!occupancy_claim(&occupancy, stroll, at(1, 1)));
    CHECK(occupancy_can_stop(&occupancy, busy, at(1, 1)));
    CHECK(occupancy_claim(&occupancy, busy, at(1, 1)));
    CHECK(!occupancy_holds(&occupancy, idle, at(1, 1)));
    CHECK(occupancy_claim(&occupancy, idle, at(1, 2))); /* It holds nothing: free to go. */
    /* The busy one keeps its tile. */
    occupancy_set_yielding(&occupancy, stroll, false);
    CHECK(!occupancy_claim(&occupancy, stroll, at(1, 1)));
    /* One knocked down takes its tile from anyone. */
    occupancy_force(&occupancy, stroll, at(1, 1));
    CHECK(occupancy_holds(&occupancy, stroll, at(1, 1)));
    CHECK(!occupancy_holds(&occupancy, busy, at(1, 1)));
    occupancy_free(&occupancy);
}

static void test_presence_counts(void)
{
    Occupancy occupancy;
    REQUIRE(occupancy_init(&occupancy, 8, 8, 1));
    BeingId a = occupancy_settler(0), b = occupancy_settler(1);
    occupancy_place(&occupancy, a, at(3, 3));
    occupancy_place(&occupancy, b, at(3, 3));
    CHECK_EQ_INT(occupancy_others_on(&occupancy, a, at(3, 3)), 1);
    CHECK_EQ_INT(occupancy_others_on(&occupancy, OCCUPANCY_NOBODY, at(3, 3)), 2);
    occupancy_place(&occupancy, b, at(3, 4));
    CHECK_EQ_INT(occupancy_others_on(&occupancy, a, at(3, 3)), 0);
    CHECK_EQ_INT(occupancy_others_on(&occupancy, a, at(3, 4)), 1);
    occupancy_place(&occupancy, b, at(3, 4)); /* Counted once. */
    CHECK_EQ_INT(occupancy_others_on(&occupancy, a, at(3, 4)), 1);
    CHECK(occupancy_claim(&occupancy, b, at(3, 4)));
    occupancy_remove(&occupancy, b);
    CHECK_EQ_INT(occupancy_others_on(&occupancy, a, at(3, 4)), 0);
    CHECK_EQ_INT(occupancy_holder(&occupancy, at(3, 4)), OCCUPANCY_NOBODY);
    occupancy_free(&occupancy);
}

static void test_stop_filter(void)
{
    Occupancy occupancy;
    REQUIRE(occupancy_init(&occupancy, 8, 8, 1));
    BeingId a = occupancy_settler(0), idle = occupancy_settler(1), busy = occupancy_animal(2);
    occupancy_set_yielding(&occupancy, idle, true);
    CHECK(occupancy_claim(&occupancy, idle, at(1, 1)));
    CHECK(occupancy_claim(&occupancy, busy, at(2, 1)));
    CHECK(occupancy_claim(&occupancy, a, at(3, 1)));
    OccupancyStopper stopper = {&occupancy, a};
    CHECK(occupancy_stop_filter(NULL, 0, 1, 0, &stopper));  /* Free. */
    CHECK(occupancy_stop_filter(NULL, 1, 1, 0, &stopper));  /* Yields. */
    CHECK(!occupancy_stop_filter(NULL, 2, 1, 0, &stopper)); /* Busy there. */
    CHECK(occupancy_stop_filter(NULL, 3, 1, 0, &stopper));  /* Its own. */
    occupancy_free(&occupancy);
}

/* A small game on bare flat soil, no one in it yet. */
static bool init_bare_game(Game *game, uint64_t seed)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = seed;
    params.world.width = 32;
    params.world.height = 32;
    params.colonist_count = 0;
    params.wildlife = 0.0f;
    if (!game_init(game, &params)) {
        return false;
    }
    for (size_t i = 0; i < map_level_tile_count(&game->map); i++) {
        game->map.terrain[i] = TERRAIN_SOIL;
        game->map.rock[i] = ROCK_NONE;
        memset(&game->map.plants[i], 0, sizeof(game->map.plants[i]));
    }
    map_topology_changed(&game->map);
    return true;
}

typedef struct Standing {
    TilePos tile[OCCUPANCY_BEINGS];
    int count;
} Standing;

/* Where those who stand still (no path, on a tile center) are. */
static void standing(const Game *game, Standing *out)
{
    out->count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) && !colonist_is_moving(colonist) &&
            colonist->move_progress == 0.0f) {
            out->tile[out->count++] =
                (TilePos){(int16_t)colonist->x, (int16_t)colonist->y, (int16_t)colonist->z};
        }
    }
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        const Animal *animal = &game->animals.list[slot];
        if (!entity_id_is_none(animals_id_at(&game->animals, slot)) && !animal_is_moving(animal) &&
            animal->move_progress == 0.0f) {
            out->tile[out->count++] =
                (TilePos){(int16_t)animal->x, (int16_t)animal->y, (int16_t)animal->z};
        }
    }
}

static int shared_tiles(const Game *game)
{
    static Standing all;
    standing(game, &all);
    int shared = 0;
    for (int i = 0; i < all.count; i++) {
        for (int j = i + 1; j < all.count; j++) {
            shared += all.tile[i].x == all.tile[j].x && all.tile[i].y == all.tile[j].y &&
                      all.tile[i].z == all.tile[j].z;
        }
    }
    return shared;
}

static void test_crowd_spreads_out(void)
{
    static Game game;
    REQUIRE(init_bare_game(&game, 11));
    for (int i = 0; i < 6; i++) {
        CHECK(!entity_id_is_none(game_spawn_colonist(&game, 16, 16, 0)));
    }
    for (int i = 0; i < 4; i++) {
        CHECK(!entity_id_is_none(
            game_spawn_animal(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 10, 10, 0)));
    }
    CHECK(shared_tiles(&game) > 0);
    for (int tick = 0; tick < 2 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
    }
    /* Then never two standing on one tile. */
    int worst = 0;
    for (int tick = 0; tick < 60 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
        int shared = shared_tiles(&game);
        worst = shared > worst ? shared : worst;
    }
    CHECK_EQ_INT(worst, 0);
    game_shutdown(&game);
}

static void test_ordered_to_one_tile(void)
{
    static Game game;
    REQUIRE(init_bare_game(&game, 12));
    EntityId ids[5];
    for (int i = 0; i < 5; i++) {
        ids[i] = game_spawn_colonist(&game, 4 + 2 * i, 4, 0);
        REQUIRE(!entity_id_is_none(ids[i]));
    }
    game_tick(&game);
    /* One by one, each to the same tile: they end up side by side. */
    for (int i = 0; i < 5; i++) {
        CHECK_EQ_INT(game_order_move(&game, &ids[i], 1, 20, 20, 0), 1);
    }
    /* Where each stops at the end of its walk. */
    TilePos end[5];
    bool arrived[5] = {false};
    int arrivals = 0, shared = 0;
    for (int tick = 0; tick < 20 * TICKS_PER_SECOND && arrivals < 5; tick++) {
        game_tick(&game);
        shared += shared_tiles(&game);
        for (int i = 0; i < 5; i++) {
            const Colonist *colonist = game_colonist(&game, ids[i]);
            if (!arrived[i] && colonist->task == TASK_MOVE && !colonist_is_moving(colonist)) {
                arrived[i] = true;
                arrivals++;
                end[i] = (TilePos){(int16_t)colonist->x, (int16_t)colonist->y, 0};
            }
        }
    }
    REQUIRE(arrivals == 5);
    for (int i = 0; i < 5; i++) {
        CHECK(abs(end[i].x - 20) <= 2 && abs(end[i].y - 20) <= 2);
    }
    CHECK_EQ_INT(shared, 0); /* Never two standing on one tile. */
    game_shutdown(&game);
}

/* Someone standing in a one tile wide corridor never blocks it. */
static void test_corridor_never_blocked(void)
{
    static Game game;
    REQUIRE(init_bare_game(&game, 13));
    for (int x = 2; x < 30; x++) {
        for (int y = 0; y < 32; y++) {
            if (y != 10) {
                map_set_rock(&game.map, x, y, 0, ROCK_GRANITE);
            }
        }
    }
    EntityId sitter = game_spawn_colonist(&game, 15, 10, 0);
    EntityId walker = game_spawn_colonist(&game, 1, 10, 0);
    REQUIRE(!entity_id_is_none(sitter) && !entity_id_is_none(walker));
    game_tick(&game);
    CHECK_EQ_INT(game_order_move(&game, &sitter, 1, 15, 10, 0), 1);
    CHECK_EQ_INT(game_order_move(&game, &walker, 1, 30, 10, 0), 1);
    bool arrived = false;
    for (int tick = 0; tick < 30 * TICKS_PER_SECOND && !arrived; tick++) {
        game_tick(&game);
        const Colonist *colonist = game_colonist(&game, walker);
        arrived = colonist->x == 30 && colonist->y == 10;
    }
    CHECK(arrived);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_one_per_tile);
    RUN_TEST(test_yielding_holders_make_way);
    RUN_TEST(test_presence_counts);
    RUN_TEST(test_stop_filter);
    RUN_TEST(test_crowd_spreads_out);
    RUN_TEST(test_ordered_to_one_tile);
    RUN_TEST(test_corridor_never_blocked);
    return test_summary();
}
