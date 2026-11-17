/* Roofs: falling in without support (core/roof.h), and planned on the level
 * above the sites of walls and beds (core/construction.h). */
#include "core/game.h"
#include "core/roof.h"

#include "test_framework.h"

#include <string.h>

static void test_natural_roof(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 32, 3));
    /* A hill: solid rock on level 0 under a floor on level 1, with a 3 × 3
     * cave dug into it. */
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            map.rock[map_index(&map, x, y, 0)] = ROCK_GRANITE;
            map.terrain[map_index(&map, x, y, 1)] = TERRAIN_SOIL;
        }
    }
    for (int y = 10; y <= 12; y++) {
        for (int x = 10; x <= 12; x++) {
            map.rock[map_index(&map, x, y, 0)] = ROCK_NONE;
        }
    }
    map_topology_changed(&map);
    CHECK(!map_natural_roof(&map, 9, 9, 0)); /* Rock itself. */
    CHECK(!map_natural_roof(&map, -1, 0, 0));
    CHECK(!map_natural_roof(&map, 11, 11, 1)); /* The hilltop: open sky. */
    for (int y = 10; y <= 12; y++) {
        for (int x = 10; x <= 12; x++) {
            CHECK(map_natural_roof(&map, x, y, 0));
            CHECK(map_is_roofed(&map, x, y, 0));
        }
    }

    /* At the foot of a cliff, open sky. */
    for (int y = 20; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            map.rock[map_index(&map, x, y, 0)] = ROCK_NONE;
            map.terrain[map_index(&map, x, y, 1)] = TERRAIN_OPEN;
        }
    }
    map_topology_changed(&map);
    CHECK(!map_natural_roof(&map, 16, 20, 0));
    CHECK(!map_natural_roof(&map, 16, 25, 0));
    /* Anything higher up covers too: rock two levels up. */
    map.rock[map_index(&map, 16, 25, 2)] = ROCK_SANDSTONE;
    CHECK(map_natural_roof(&map, 16, 25, 0));
    CHECK(map_natural_roof(&map, 16, 25, 1));
    /* A built roof counts as well, but is not natural. */
    CHECK(!map_is_roofed(&map, 17, 25, 0));
    map_set_roof(&map, 17, 25, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK(map_is_roofed(&map, 17, 25, 0));
    CHECK(!map_natural_roof(&map, 17, 25, 0));
    CHECK_EQ_INT(map_roof_over(&map, 17, 25, 0), STRUCTURE_ROOF);
    CHECK_EQ_INT(map_roof_over(&map, 16, 25, 0), STRUCTURE_NONE); /* Rock. */
    map_free(&map);
}

static void test_collapse(void)
{
    Map map;
    Items items;
    Designations designations;
    REQUIRE(map_init(&map, 24, 24, 2));
    REQUIRE(items_init(&items, 24, 24, 2));
    REQUIRE(designations_init(&designations, 24, 24, 2));
    RoofWatch watch;
    roof_watch_init(&watch, &map);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations, NULL), 0);

    /* Two walls; on the level above, a roof held by both, one held by the
     * first only, one by the second only, at the edge of its reach. */
    map_set_structure(&map, 5, 5, 0, STRUCTURE_WALL);
    map_set_structure(&map, 9, 5, 0, STRUCTURE_WALL);
    map_set_roof(&map, 7, 5, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    map_set_roof(&map, 4, 6, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    map_set_roof(&map, 9 + ROOF_SUPPORT_RANGE, 5 + ROOF_SUPPORT_RANGE, 1, STRUCTURE_ROOF,
                 ITEM_QUALITY_STANDARD);
    CHECK(designation_set(&designations, &map, 4, 6, 1, DESIGNATION_DECONSTRUCT));
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations, NULL), 0); /* All held up. */

    /* The first wall goes: the roof it alone held falls in, half of its
     * materials on the floor under it, and the order to take it down goes
     * with it. */
    map_set_structure(&map, 5, 5, 0, STRUCTURE_NONE);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations, NULL), 1);
    CHECK_EQ_INT(map_roof(&map, 4, 6, 1), STRUCTURE_NONE);
    CHECK(map_is_open(&map, 4, 6, 1));
    CHECK_EQ_INT(map_roof(&map, 7, 5, 1), STRUCTURE_ROOF);
    CHECK_EQ_INT(map.roof_count, 2);
    const StructureDef *roof = structure_def(STRUCTURE_ROOF);
    CHECK_EQ_INT((int)items.totals[roof->materials[0].type], roof->materials[0].count / 2);
    CHECK_EQ_INT((int)items.totals[roof->materials[1].type], roof->materials[1].count / 2);
    CHECK_EQ_INT(items_get(&items, 4, 6, 0).count > 0, 1);
    CHECK_EQ_INT((int)items.level_stacks[1], 0);
    CHECK_EQ_INT(designation_get(&designations, 4, 6, 1), DESIGNATION_NONE);
    CHECK_EQ_INT(designations.count, 0);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, &designations, NULL), 0); /* Nothing new. */

    /* A door holds nothing up: the second wall turned into a door. */
    map_set_structure(&map, 9, 5, 0, STRUCTURE_DOOR);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL, NULL), 2);
    CHECK_EQ_INT(map.roof_count, 0);

    /* Rock holds roofs up too, until it is dug out. */
    map_set_rock(&map, 15, 15, 0, ROCK_GRANITE);
    map_set_roof(&map, 16, 16, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL, NULL), 0);
    map_set_rock(&map, 15, 15, 0, ROCK_NONE);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL, NULL), 1);

    designations_free(&designations);
    items_free(&items);
    map_free(&map);
}

/* More changes than the map remembers at once: every roof is checked. */
static void test_collapse_after_many_changes(void)
{
    Map map;
    REQUIRE(map_init(&map, 40, 40, 2));
    RoofWatch watch;
    map_set_structure(&map, 2, 2, 0, STRUCTURE_WALL);
    map_set_roof(&map, 3, 3, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    roof_watch_init(&watch, &map);
    map_set_structure(&map, 2, 2, 0, STRUCTURE_NONE);
    for (int i = 0; i < 2 * MAP_BUILD_LOG_SIZE; i++) { /* Far away. */
        map_set_structure(&map, 30, 30, 0, i % 2 == 0 ? STRUCTURE_WALL : STRUCTURE_NONE);
    }
    CHECK_EQ_INT(roofs_collapse(&watch, &map, NULL, NULL, NULL), 1);
    CHECK_EQ_INT(map.roof_count, 0);
    map_free(&map);
}

/* 16 x 16, 3 levels: plains on level `floor`, rock under them; a hill of
 * earth for x >= 4 one level higher; a 5 x 5 room dug into it (x and y 5 to
 * 9) on level `floor`, held up by a prop in its middle (7, 7), the one tile
 * out of reach of the rock around. */
static bool init_propped_room(Map *map, int floor)
{
    if (!map_init(map, 16, 16, 3)) {
        return false;
    }
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            bool hill = x >= 4, room = x >= 5 && x <= 9 && y >= 5 && y <= 9;
            for (int z = 0; z < 3; z++) {
                size_t i = map_index(map, x, y, z);
                bool solid = z < floor || (z == floor && hill && !room);
                map->terrain[i] =
                    (uint8_t)(z <= floor || (z == floor + 1 && hill) ? TERRAIN_SOIL : TERRAIN_OPEN);
                map->rock[i] = (uint8_t)(solid ? ROCK_EARTH : ROCK_NONE);
            }
        }
    }
    map_topology_changed(map); /* The layers were written directly. */
    map_set_structure(map, 7, 7, floor, STRUCTURE_PROP);
    return true;
}

/* A prop taken away: the ceiling it held caves in, the tile fills up again
 * (with the rock above it), what lay there is pushed beside it. */
static void test_cave_in(void)
{
    Map map;
    Items items;
    REQUIRE(init_propped_room(&map, 0));
    REQUIRE(items_init(&items, 16, 16, 3));
    map_set_rock(&map, 7, 7, 1, ROCK_GRANITE); /* A boulder in the ground above. */
    RoofWatch watch;
    roof_watch_init(&watch, &map);
    CaveIns cave_ins;
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, NULL, &cave_ins), 0);
    CHECK_EQ_INT(cave_ins.count, 0);

    map_set_structure(&map, 7, 7, 0, STRUCTURE_NONE);
    REQUIRE(items_add(&items, 7, 7, 0, item_stack_pristine(ITEM_WOOD, 3)) == 3);
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, NULL, &cave_ins), 0);
    REQUIRE(cave_ins.count == 1);
    CHECK(cave_ins.tiles[0].x == 7 && cave_ins.tiles[0].y == 7 && cave_ins.tiles[0].z == 0);
    CHECK_EQ_INT(map.rock[map_index(&map, 7, 7, 0)], ROCK_GRANITE);
    CHECK_EQ_INT(items_get(&items, 7, 7, 0).count, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 3);
    CHECK_EQ_INT(items_get(&items, 7, 6, 0).count, 3);
    /* The rest of the room is within reach of the rock, the new rock too. */
    CHECK_EQ_INT(roofs_collapse(&watch, &map, &items, NULL, &cave_ins), 0);
    CHECK_EQ_INT(cave_ins.count, 0);
    CHECK(map_is_passable(&map, 6, 6, 0));
    items_free(&items);
    map_free(&map);
}

/* More changes than the map remembers at once: every level is checked. */
static void test_cave_in_after_many_changes(void)
{
    Map map;
    REQUIRE(init_propped_room(&map, 1));
    RoofWatch watch;
    roof_watch_init(&watch, &map);
    map_set_structure(&map, 7, 7, 1, STRUCTURE_NONE);
    for (int i = 0; i < 2 * MAP_BUILD_LOG_SIZE; i++) { /* Far away. */
        map_set_structure(&map, 1, 14, 1, i % 2 == 0 ? STRUCTURE_WALL : STRUCTURE_NONE);
    }
    CaveIns cave_ins;
    roofs_collapse(&watch, &map, NULL, NULL, &cave_ins);
    REQUIRE(cave_ins.count == 1);
    CHECK_EQ_INT(cave_ins.tiles[0].z, 1);
    CHECK(map.rock[map_index(&map, 7, 7, 1)] == ROCK_EARTH);
    map_free(&map);
}

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true; /* One level of ground, open air over it. */
    params.world.seed = 11;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN; /* Open air over it: no trees rising there. */
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) { /* The ground level. */
        map->terrain[i] = TERRAIN_SOIL;
        map->cobbles[i] = 0;
        map->flint[i] = 0;
        map->branches[i] = map->twigs[i] = 0;
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
    map_set_roof(&game.map, 11, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_roof(&game.map, 11, 10, 1), STRUCTURE_ROOF);
    uint32_t notices = game.notices.count;
    map_set_structure(&game.map, 10, 10, 0, STRUCTURE_NONE);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_roof(&game.map, 11, 10, 1), STRUCTURE_NONE);
    CHECK(game.items.totals[ITEM_GRASS] > 0);
    REQUIRE(game.notices.count == notices + 1);
    CHECK_EQ_INT(notices_get(&game.notices, notices)->type, NOTICE_ROOF_COLLAPSED);
    game_shutdown(&game);
}

/* Under the fog the colony hears of a roof falling only when someone sees it. */
static void test_collapse_notice_needs_sight(void)
{
    for (int watched = 0; watched < 2; watched++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        game.clock.tick = 12 * TICKS_PER_HOUR; /* Noon: nobody asleep. */
        map_set_structure(&game.map, 10, 10, 0, STRUCTURE_WALL);
        map_set_roof(&game.map, 11, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
        if (watched) {
            REQUIRE(game_spawn_colonist(&game, 14, 10, 0).generation != 0);
        }
        game.arrival = (TilePos){40, 40, 0};
        game.has_arrival = true;
        REQUIRE(game_set_fog(&game, true));
        run_ticks(&game, 2);
        uint32_t notices = game.notices.count;
        map_set_structure(&game.map, 10, 10, 0, STRUCTURE_NONE);
        run_ticks(&game, 1);
        CHECK_EQ_INT(map_roof(&game.map, 11, 10, 1), STRUCTURE_NONE);
        CHECK_EQ_INT(game.notices.count, notices + (watched ? 1 : 0));
        game_shutdown(&game);
    }
}

/* A roof planned over the site of a wall (and a wall under the site of a
 * roof): both are built, each by its own builder or one after the other,
 * the roofs from below. Cancelling works on one level at a time. */
static void test_roof_over_planned_wall(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int wall = game_place_structure(&game, STRUCTURE_WALL, 20, 20, 0, DIRECTION_SOUTH);
    REQUIRE(wall != CONSTRUCTION_NONE);
    CHECK_EQ_INT(
        game_place_structure_rect(&game, STRUCTURE_ROOF, DIRECTION_SOUTH, 20, 20, 22, 20, 1), 3);
    int roof = construction_at(&game.constructions, 20, 20, 1);
    REQUIRE(roof != CONSTRUCTION_NONE);
    CHECK(roof != wall);
    CHECK_EQ_INT(construction_at(&game.constructions, 20, 20, 0), wall);
    CHECK(construction_is_roof(construction_get(&game.constructions, roof)));
    CHECK(!construction_is_roof(construction_get(&game.constructions, wall)));
    CHECK_EQ_INT(construction_worked_from(&game.constructions, 20, 20, 0, true), roof);
    CHECK_EQ_INT(construction_worked_from(&game.constructions, 20, 20, 0, false), wall);
    /* Not twice on a tile; no roof on the ground. */
    CHECK(!game_can_place_structure(&game, STRUCTURE_ROOF, 20, 20, 1, DIRECTION_SOUTH));
    CHECK(!game_can_place_structure(&game, STRUCTURE_ROOF, 23, 20, 0, DIRECTION_SOUTH));
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, 20, 20, 0, DIRECTION_SOUTH));
    /* A bed planned under a roof's site. */
    CHECK(game_can_place_structure(&game, STRUCTURE_GRASS_BED, 22, 20, 0, DIRECTION_SOUTH));

    /* Cancelling on the roofs' level leaves the wall, and the other way round. */
    CHECK_EQ_INT(game_cancel_rect(&game, 22, 20, 22, 20, 1), 1);
    CHECK_EQ_INT(construction_at(&game.constructions, 22, 20, 1), CONSTRUCTION_NONE);
    CHECK_EQ_INT(game_cancel_rect(&game, 21, 20, 21, 20, 0), 0); /* Nothing on the ground. */
    CHECK(construction_at(&game.constructions, 21, 20, 1) != CONSTRUCTION_NONE);
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
    CHECK_EQ_INT(map_roof(&game.map, 20, 20, 1), STRUCTURE_ROOF);
    CHECK_EQ_INT(map_roof(&game.map, 21, 20, 1), STRUCTURE_ROOF);
    CHECK_EQ_INT(game.map.roof_count, 2);
    for (uint32_t slot = 0; slot < game_colonist_slot_count(&game); slot++) {
        CHECK_EQ_INT(game.colonists[slot].z, 0); /* Built from below. */
    }
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_natural_roof);
    RUN_TEST(test_collapse);
    RUN_TEST(test_collapse_after_many_changes);
    RUN_TEST(test_cave_in);
    RUN_TEST(test_cave_in_after_many_changes);
    RUN_TEST(test_collapse_in_game);
    RUN_TEST(test_collapse_notice_needs_sight);
    RUN_TEST(test_roof_over_planned_wall);
    return test_summary();
}
