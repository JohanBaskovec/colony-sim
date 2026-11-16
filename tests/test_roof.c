/* Roofs: falling in without support (core/roof.h), and planned in a layer of
 * their own over the sites of walls and beds (core/construction.h). */
#include "core/game.h"
#include "core/roof.h"

#include "test_framework.h"

#include <string.h>

static void test_collapse(void)
{
    Map map;
    Items items;
    Designations designations;
    REQUIRE(map_init(&map, 24, 24, 1));
    REQUIRE(items_init(&items, 24, 24, 1));
    REQUIRE(designations_init(&designations, 24, 24, 1));
    RoofWatch watch;
    roof_watch_init(&watch, &map);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations), 0);

    /* Two walls; a roof held by both, one held by the first only, one by the
     * second only, at the edge of its reach. */
    map_set_structure(&map, 5, 5, 0, STRUCTURE_WALL);
    map_set_structure(&map, 9, 5, 0, STRUCTURE_WALL);
    map_set_roof(&map, 7, 5, 0, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    map_set_roof(&map, 4, 6, 0, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    map_set_roof(&map, 9 + ROOF_SUPPORT_RANGE, 5 + ROOF_SUPPORT_RANGE, 0, STRUCTURE_ROOF,
                 ITEM_QUALITY_STANDARD);
    CHECK(designation_set(&designations, &map, 4, 6, 0, DESIGNATION_REMOVE_ROOF));
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations), 0); /* All held up. */

    /* The first wall goes: the roof it alone held falls in, half of its
     * materials on the tile, and the order to take it down goes with it. */
    map_set_structure(&map, 5, 5, 0, STRUCTURE_NONE);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations), 1);
    CHECK_EQ_INT(map_roof(&map, 4, 6, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_roof(&map, 7, 5, 0), STRUCTURE_ROOF);
    CHECK_EQ_INT(map.roof_count, 2);
    const StructureDef *roof = structure_def(STRUCTURE_ROOF);
    CHECK_EQ_INT((int)items.totals[roof->materials[0].type], roof->materials[0].count / 2);
    CHECK_EQ_INT((int)items.totals[roof->materials[1].type], roof->materials[1].count / 2);
    CHECK_EQ_INT(designation_get(&designations, 4, 6, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(designations.count, 0);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations), 0); /* Nothing new. */

    /* A door holds nothing up: the second wall turned into a door. */
    map_set_structure(&map, 9, 5, 0, STRUCTURE_DOOR);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL), 2);
    CHECK_EQ_INT(map.roof_count, 0);

    /* Rock holds roofs up too, until it is dug out. */
    map_set_rock(&map, 15, 15, 0, ROCK_GRANITE);
    map_set_roof(&map, 16, 16, 0, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL), 0);
    map_set_rock(&map, 15, 15, 0, ROCK_NONE);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL), 1);

    designations_free(&designations);
    items_free(&items);
    map_free(&map);
}

/* More changes than the map remembers at once: every roof is checked. */
static void test_collapse_after_many_changes(void)
{
    Map map;
    REQUIRE(map_init(&map, 40, 40, 1));
    RoofWatch watch;
    map_set_structure(&map, 2, 2, 0, STRUCTURE_WALL);
    map_set_roof(&map, 3, 3, 0, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    roof_watch_init(&watch, &map);
    map_set_structure(&map, 2, 2, 0, STRUCTURE_NONE);
    for (int i = 0; i < 2 * MAP_BUILD_LOG_SIZE; i++) { /* Far away. */
        map_set_structure(&map, 30, 30, 0, i % 2 == 0 ? STRUCTURE_WALL : STRUCTURE_NONE);
    }
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL), 1);
    CHECK_EQ_INT(map.roof_count, 0);
    map_free(&map);
}

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true; /* One level, as the tiles below expect. */
    params.world.seed = 11;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->cobbles[i] = 0;
        map->flint[i] = 0;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

static Colonist *spawn_builder(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            colonist_set_knowledge_level(colonist, (KnowledgeType)k, 75.0);
        }
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
    }
    return colonist;
}

/* Rested and fed, always at noon: long runs are only about work. */
static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
                for (int need = 0; need < NEED_COUNT; need++) {
                    game->colonists[slot].needs[need] = 1.0f;
                }
            }
        }
        game->clock.tick = 12 * TICKS_PER_HOUR;
        game_tick(game);
    }
}

/* A wall taken apart in a game: the roof it held falls in, with a notice. */
static void test_collapse_in_game(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 10, 10, 0, STRUCTURE_WALL);
    map_set_roof(&game.map, 11, 10, 0, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_roof(&game.map, 11, 10, 0), STRUCTURE_ROOF);
    uint32_t notices = game.notices.count;
    map_set_structure(&game.map, 10, 10, 0, STRUCTURE_NONE);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_roof(&game.map, 11, 10, 0), STRUCTURE_NONE);
    CHECK(game.items.totals[ITEM_GRASS] > 0);
    REQUIRE(game.notices.count == notices + 1);
    CHECK_EQ_INT(notices_get(&game.notices, notices)->type, NOTICE_ROOF_COLLAPSED);
    game_shutdown(&game);
}

/* A roof planned over the site of a wall (and a wall under the site of a
 * roof): both are built, each by its own builder or one after the other.
 * Cancelling works on one layer at a time. */
static void test_roof_over_planned_wall(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int wall = game_place_structure(&game, STRUCTURE_WALL, 20, 20, 0, DIRECTION_SOUTH);
    REQUIRE(wall != CONSTRUCTION_NONE);
    CHECK_EQ_INT(game_place_structure_rect(&game, STRUCTURE_ROOF, DIRECTION_SOUTH, 20, 20, 22,
                                           20, 0),
                 3);
    int roof = construction_roof_at(&game.constructions, 20, 20, 0);
    REQUIRE(roof != CONSTRUCTION_NONE);
    CHECK(roof != wall);
    CHECK_EQ_INT(construction_at(&game.constructions, 20, 20, 0), wall);
    CHECK(construction_is_roof(construction_get(&game.constructions, roof)));
    CHECK(!construction_is_roof(construction_get(&game.constructions, wall)));
    CHECK_EQ_INT(construction_in_layer(&game.constructions, 20, 20, 0, true), roof);
    CHECK_EQ_INT(construction_in_layer(&game.constructions, 20, 20, 0, false), wall);
    /* Not twice in the same layer. */
    CHECK(!game_can_place_structure(&game, STRUCTURE_ROOF, 20, 20, 0, DIRECTION_SOUTH));
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, 20, 20, 0, DIRECTION_SOUTH));
    /* A bed planned under a roof's site. */
    CHECK(game_can_place_structure(&game, STRUCTURE_GRASS_BED, 22, 20, 0, DIRECTION_SOUTH));

    /* Cancelling roofs leaves the wall, and the other way round. */
    CHECK_EQ_INT(game_cancel_rect(&game, 22, 20, 22, 20, 0, true), 1);
    CHECK_EQ_INT(construction_roof_at(&game.constructions, 22, 20, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(game_cancel_rect(&game, 21, 20, 21, 20, 0, false), 0); /* Nothing on the ground. */
    CHECK_EQ_INT(construction_roof_at(&game.constructions, 21, 20, 0) != CONSTRUCTION_NONE, 1);
    CHECK_EQ_INT(game.constructions.used_count, 3);

    /* Two builders and the materials: the wall, then the roofs it holds. */
    REQUIRE(spawn_builder(&game, 14, 14) != NULL);
    REQUIRE(spawn_builder(&game, 15, 14) != NULL);
    items_add(&game.items, 12, 12, 0, item_stack_pristine(ITEM_BRANCHES, 11));
    for (int x = 8; x < 12; x++) {
        items_add(&game.items, x, 8, 0, item_stack_pristine(ITEM_GRASS, 20));
    }
    for (int i = 0; i < 12 * TICKS_PER_HOUR && game.constructions.used_count > 0; i++) {
        run_ticks(&game, 1);
    }
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK_EQ_INT(map_structure(&game.map, 20, 20, 0), STRUCTURE_WALL);
    CHECK_EQ_INT(map_roof(&game.map, 20, 20, 0), STRUCTURE_ROOF);
    CHECK_EQ_INT(map_roof(&game.map, 21, 20, 0), STRUCTURE_ROOF);
    CHECK_EQ_INT(game.map.roof_count, 2);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_collapse);
    RUN_TEST(test_collapse_after_many_changes);
    RUN_TEST(test_collapse_in_game);
    RUN_TEST(test_roof_over_planned_wall);
    return test_summary();
}
