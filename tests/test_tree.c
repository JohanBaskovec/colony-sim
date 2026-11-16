/* Trees in the volume: trunks of wood, floors of foliage, climbing them, how
 * they grow, and how felled trees fall (core/tree.h). */
#include "core/tree.h"

#include "core/designation.h"
#include "core/game.h"
#include "core/gather_zone.h"
#include "core/pathfinding.h"
#include "core/worldgen.h"

#include "test_framework.h"

#include <string.h>

#define SIZE 20
#define DEPTH 9

/* Growths of a tree at each stage of interest. */
#define SAPLING 0.3f     /* Stage 1: a plant like the others. */
#define YOUNG 0.45f      /* Stage 2: trunk 1 level. */
#define HALF_GROWN 0.65f /* Stage 3: trunk 2 levels. */
#define GROWN 1.0f       /* Stage 4: trunk 3 levels. */

/* Soil on level 0, open space above. */
static bool init_world(Map *map, Items *items)
{
    return map_init(map, SIZE, SIZE, DEPTH) && items_init(items, SIZE, SIZE, DEPTH);
}

static void put_tree(Map *map, int x, int y, int z, float growth)
{
    map->plants[map_index(map, x, y, z)] = (Plant){.type = PLANT_TREE, .growth = growth};
}

/* A tree written into the map. */
static void grow_tree(Map *map, int x, int y, int z, float growth, int size)
{
    map->plants[map_index(map, x, y, z)] =
        (Plant){.type = PLANT_TREE, .size = (uint8_t)size, .growth = growth};
    tree_build(map, x, y, z);
}

static bool is_trunk(const Map *map, int x, int y, int z)
{
    return map->rock[map_index(map, x, y, z)] == ROCK_WOOD;
}

static bool is_foliage(const Map *map, int x, int y, int z)
{
    return map->terrain[map_index(map, x, y, z)] == TERRAIN_FOLIAGE &&
           map->rock[map_index(map, x, y, z)] == ROCK_NONE;
}

static int count_on_line(const Items *items, int x, int y, int z, int dx, int dy, int from, int to,
                         ItemType type)
{
    int count = 0;
    for (int i = from; i <= to; i++) {
        ItemStack stack = items_get(items, x + dx * i, y + dy * i, z);
        count += stack.type == type ? stack.count : 0;
    }
    return count;
}

static void test_shapes(void)
{
    Plant plant = {.type = PLANT_TREE, .growth = SAPLING};
    TreeShape shape;
    CHECK(!tree_shape(&plant, &shape)); /* Saplings take no room. */
    plant.growth = 0.1f;
    CHECK(!tree_shape(&plant, &shape));
    plant.growth = YOUNG;
    REQUIRE(tree_shape(&plant, &shape));
    CHECK_EQ_INT(shape.size, 1);
    CHECK_EQ_INT(shape.trunk, 1);
    CHECK_EQ_INT(shape.foliage_from, 1);
    CHECK_EQ_INT(tree_shape_levels(&shape), 2);
    plant.growth = HALF_GROWN;
    REQUIRE(tree_shape(&plant, &shape));
    CHECK_EQ_INT(shape.trunk, 2);
    plant.growth = GROWN;
    REQUIRE(tree_shape(&plant, &shape));
    CHECK_EQ_INT(shape.trunk, 3);
    CHECK_EQ_INT(shape.reach, 1);
    CHECK_EQ_INT(tree_size(&plant), 1);

    plant.size = 2; /* Old: a bare trunk under its crown. */
    REQUIRE(tree_shape(&plant, &shape));
    CHECK_EQ_INT(shape.size, 2);
    CHECK_EQ_INT(shape.reach, 2);
    CHECK(shape.foliage_from > 1);
    CHECK_EQ_INT(tree_size(&plant), 2);
    plant.size = 3; /* Ancient: the tallest. */
    REQUIRE(tree_shape(&plant, &shape));
    CHECK_EQ_INT(shape.size, 3);
    CHECK_EQ_INT(tree_shape_levels(&shape), TREE_MAX_LEVELS);
    CHECK(shape.reach <= TREE_MAX_REACH);

    Plant hazel = {.type = PLANT_HAZEL, .growth = 1.0f};
    CHECK(!tree_shape(&hazel, &shape));
    CHECK_EQ_INT(tree_size(&hazel), 0);

    /* Where trunk and foliage are, from the foot. */
    plant = (Plant){.type = PLANT_TREE, .growth = GROWN};
    REQUIRE(tree_shape(&plant, &shape));
    CHECK(tree_trunk_holds(&shape, 0, 0, 0));
    CHECK(tree_trunk_holds(&shape, 0, 0, 2));
    CHECK(!tree_trunk_holds(&shape, 0, 0, 3));
    CHECK(!tree_trunk_holds(&shape, 1, 0, 0));
    CHECK(!tree_foliage_holds(&shape, 0, 0, 1)); /* The trunk is there. */
    CHECK(tree_foliage_holds(&shape, 0, 0, 3));  /* Over the top of the trunk. */
    CHECK(tree_foliage_holds(&shape, 1, 0, 1));
    CHECK(tree_foliage_holds(&shape, -1, -1, 2)); /* A small crown is square. */
    CHECK(!tree_foliage_holds(&shape, 2, 0, 1));
    CHECK(!tree_foliage_holds(&shape, 1, 0, 0)); /* Nothing on the ground. */
    CHECK(!tree_foliage_holds(&shape, 1, 0, 4));
    plant.size = 3; /* A wide crown is rounded. */
    REQUIRE(tree_shape(&plant, &shape));
    CHECK(tree_foliage_holds(&shape, -3, 0, shape.trunk));
    CHECK(tree_foliage_holds(&shape, -2, -2, shape.trunk));
    CHECK(!tree_foliage_holds(&shape, -3, -3, shape.trunk));
    CHECK(!tree_foliage_holds(&shape, -3, -2, shape.trunk));
    CHECK(!tree_foliage_holds(&shape, 0, -1, shape.foliage_from - 1));
}

static void test_build_and_find(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    /* The trunk: wood on three levels, nothing under it over the foot. */
    for (int z = 0; z < 3; z++) {
        CHECK(is_trunk(&map, 10, 10, z));
    }
    CHECK(!is_trunk(&map, 10, 10, 3));
    CHECK_EQ_INT(map.terrain[map_index(&map, 10, 10, 1)], TERRAIN_OPEN);
    CHECK_EQ_INT(map.terrain[map_index(&map, 10, 10, 0)], TERRAIN_SOIL);
    CHECK(!map_is_passable(&map, 10, 10, 0));
    CHECK(!map_is_passable(&map, 10, 10, 2));
    /* Foliage around it on levels 1 to 3, over it on the top. */
    CHECK(is_foliage(&map, 11, 10, 1));
    CHECK(is_foliage(&map, 9, 9, 2));
    CHECK(is_foliage(&map, 10, 10, 3));
    CHECK(is_foliage(&map, 11, 11, 3));
    CHECK(!is_foliage(&map, 12, 10, 1));
    CHECK(!is_foliage(&map, 11, 10, 0));
    CHECK(!is_foliage(&map, 11, 10, 4));
    CHECK(map_is_passable(&map, 11, 10, 1)); /* Walked on. */
    CHECK(!map_is_buildable(&map, 11, 10, 1));

    /* Found from any of its tiles. */
    TreeRef tree;
    REQUIRE(tree_at(&map, 10, 10, 2, &tree));
    CHECK_EQ_INT(tree.x, 10);
    CHECK_EQ_INT(tree.y, 10);
    CHECK_EQ_INT(tree.z, 0);
    CHECK_EQ_INT(tree.shape.trunk, 3);
    REQUIRE(tree_at(&map, 9, 9, 1, &tree));
    CHECK_EQ_INT(tree.x, 10);
    REQUIRE(tree_at(&map, 10, 10, 3, &tree));
    CHECK_EQ_INT(tree.z, 0);
    CHECK(tree_plant(&map, &tree) == &map.plants[map_index(&map, 10, 10, 0)]);
    CHECK(!tree_at(&map, 12, 10, 1, &tree));
    CHECK(!tree_at(&map, 11, 10, 0, &tree));
    CHECK(!tree_at(&map, -1, 10, 1, &tree));

    /* Foliage is no ceiling and no roof; the ground stays the ground; the
     * trunk is not mined. */
    CHECK(!map_has_ceiling(&map, 11, 10, 0));
    CHECK(map_ceiling_supported(&map, 11, 10, 0));
    CHECK_EQ_INT(map_cover_level(&map, 11, 10, 0), -1);
    CHECK(!map_is_roofed(&map, 11, 10, 1));
    CHECK_EQ_INT(map_ground_level(&map, 11, 10), 0);
    CHECK_EQ_INT(map_ground_level(&map, 10, 10), 0);
    CHECK(!designation_can_apply(&map, 10, 10, 1, DESIGNATION_MINE));
    CHECK(map_plant_blocks_building(&map, 10, 10, 0));

    /* An old tree: a 2 x 2 trunk, found from any tile of it. */
    grow_tree(&map, 3, 3, 0, GROWN, 2);
    CHECK(is_trunk(&map, 4, 4, 4));
    CHECK(is_trunk(&map, 3, 4, 0));
    CHECK(!is_trunk(&map, 5, 3, 0));
    REQUIRE(tree_at(&map, 4, 4, 2, &tree));
    CHECK_EQ_INT(tree.x, 3);
    CHECK_EQ_INT(tree.y, 3);
    CHECK_EQ_INT(tree.shape.size, 2);
    CHECK(!is_foliage(&map, 5, 3, 1)); /* A bare trunk under its crown. */
    REQUIRE(tree_at(&map, 6, 4, tree.shape.foliage_from, &tree));
    CHECK_EQ_INT(tree.x, 3);
    /* It sheds more branches and twigs (by its area), and gives more when felled. */
    Plant *old = &map.plants[map_index(&map, 3, 3, 0)];
    Plant *grown = &map.plants[map_index(&map, 10, 10, 0)];
    CHECK(tree_shed_chance(old, ITEM_TWIGS) == 4.0f * tree_shed_chance(grown, ITEM_TWIGS));
    CHECK_EQ_INT(tree_crown_yield(old, ITEM_BRANCHES), 4 * tree_crown_yield(grown, ITEM_BRANCHES));

    /* Foliage leaves the way up a slope open. */
    map_set_slope(&map, 15, 16, 0, true);
    map_set_terrain(&map, 15, 17, 1, TERRAIN_SOIL);
    grow_tree(&map, 16, 16, 0, YOUNG, 1);
    CHECK(is_foliage(&map, 17, 16, 1));
    CHECK(map_is_open(&map, 15, 16, 1));
    CHECK(map_can_climb(&map, 15, 16, 0, 15, 17));
    items_free(&items);
    map_free(&map);
}

static void test_climbing(void)
{
    Map map;
    Items items;
    Pathfinder finder;
    REQUIRE(init_world(&map, &items));
    REQUIRE(pathfinder_init(&finder, SIZE, SIZE, DEPTH));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    /* Up from beside the trunk, straight to the foliage over it. */
    CHECK(map_beside_trunk(&map, 11, 10, 0));
    CHECK(!map_beside_trunk(&map, 11, 11, 0)); /* Diagonal: not beside. */
    CHECK(map_can_climb_tree(&map, 11, 10, 0));
    CHECK(!map_can_climb_tree(&map, 11, 11, 0));
    CHECK(!map_can_climb_tree(&map, 12, 10, 0));
    CHECK(map_can_climb_tree(&map, 11, 10, 1)); /* From one floor to the next. */
    CHECK(map_can_climb_tree(&map, 11, 10, 2));
    CHECK(!map_can_climb_tree(&map, 11, 10, 3)); /* The top. */
    TilePos links[MAP_MAX_LEVEL_LINKS];
    int count = map_level_links(&map, 11, 10, 1, links);
    bool up = false, down = false;
    for (int i = 0; i < count; i++) {
        up |= links[i].x == 11 && links[i].y == 10 && links[i].z == 2;
        down |= links[i].x == 11 && links[i].y == 10 && links[i].z == 0;
    }
    CHECK(up);
    CHECK(down);
    /* A path to the top of the tree, climbing at MAP_TREE_CLIMB_COST. */
    Path path = {0};
    TilePos start = {16, 10, 0}, top = {10, 10, 3};
    REQUIRE(pathfinder_find(&finder, &map, start, top, PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_INT(path.steps[path.count - 1].z, 3);
    CHECK(finder.last_cost >= 3.0f * MAP_TREE_CLIMB_COST);
    for (int i = 1; i < path.count; i++) {
        if (path.steps[i].z != path.steps[i - 1].z) { /* Straight up, beside the trunk. */
            CHECK_EQ_INT(path.steps[i].x, path.steps[i - 1].x);
            CHECK_EQ_INT(path.steps[i].y, path.steps[i - 1].y);
        }
    }
    path_free(&path);
    pathfinder_free(&finder);
    items_free(&items);
    map_free(&map);
}

static void test_room_to_grow(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    TreeShape grown, half;
    Plant plant = {.type = PLANT_TREE, .growth = GROWN};
    REQUIRE(tree_shape(&plant, &grown));
    plant.growth = HALF_GROWN;
    REQUIRE(tree_shape(&plant, &half));
    CHECK(tree_room_to_grow(&map, 5, 5, 0, &grown));
    CHECK(!tree_room_to_grow(&map, 5, 5, DEPTH - 2, &grown)); /* Out of the map. */
    /* Under an overhang: no room for the trunk. */
    map_set_terrain(&map, 5, 5, 2, TERRAIN_SOIL);
    CHECK(tree_room_to_grow(&map, 5, 5, 0, &half));
    CHECK(!tree_room_to_grow(&map, 5, 5, 0, &grown));
    map_set_terrain(&map, 5, 5, 2, TERRAIN_OPEN);
    /* A tree grows into its own foliage. */
    grow_tree(&map, 10, 10, 0, HALF_GROWN, 1);
    CHECK(is_foliage(&map, 10, 10, 2));
    CHECK(tree_room_to_grow(&map, 10, 10, 0, &grown));
    CHECK(tree_grows_into(10, 10, 0, &grown, 10, 10, 2));
    CHECK(!tree_grows_into(10, 10, 0, &half, 10, 10, 2));
    map.plants[map_index(&map, 10, 10, 0)].growth = GROWN;
    tree_build(&map, 10, 10, 0);
    CHECK(is_trunk(&map, 10, 10, 2));
    CHECK(is_foliage(&map, 10, 10, 3));
    CHECK(is_foliage(&map, 11, 10, 1)); /* Its lower foliage stays. */
    /* Not into another tree's foliage. */
    put_tree(&map, 11, 11, 0, HALF_GROWN);
    CHECK(!tree_room_to_grow(&map, 11, 11, 0, &half));
    /* Nor through a structure. */
    map_set_structure(&map, 3, 3, 0, STRUCTURE_WALL);
    CHECK(!tree_room_to_grow(&map, 3, 3, 0, &half));
    items_free(&items);
    map_free(&map);
}

/* A small game on a flat soil map without trees nor colonists. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 7;
    params.world.width = 32;
    params.world.height = 32;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        bool ground = i < map_level_tile_count(map);
        map->terrain[i] = ground ? TERRAIN_SOIL : TERRAIN_OPEN;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
        map->branches[i] = map->twigs[i] = 0;
    }
    map_topology_changed(map);
    return true;
}

/* In the game, a sapling that grows takes room as soon as nothing is in the
 * way; one under an item waits. */
static void test_growing_in_the_game(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    float almost = 0.399f; /* An hour from stage 2. */
    put_tree(map, 5, 5, 0, almost);
    put_tree(map, 20, 20, 0, almost);
    items_add(&game.items, 20, 20, 0, item_stack_pristine(ITEM_TWIGS, 1));
    stockpile_create(&game.stockpiles, map, 4, 4, 6, 6, 0);
    game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL);
    TreeShape shape;
    CHECK(tree_shape(&map->plants[map_index(map, 5, 5, 0)], &shape));
    CHECK(is_trunk(map, 5, 5, 0));
    CHECK(is_foliage(map, 6, 5, 1));
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 5, 5, 0), -1); /* No stockpile on a trunk. */
    CHECK(stockpile_at(&game.stockpiles, 4, 4, 0) >= 0);
    /* The other waits for the twigs to go. */
    CHECK(!tree_shape(&map->plants[map_index(map, 20, 20, 0)], &shape));
    CHECK(!is_trunk(map, 20, 20, 0));
    items_remove(&game.items, 20, 20, 0, 1);
    game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL);
    CHECK(is_trunk(map, 20, 20, 0));
    game_shutdown(&game);
}

static void test_clear_and_shared_foliage(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    grow_tree(&map, 12, 10, 0, GROWN, 1); /* Their crowns meet on x = 11. */
    items_add(&items, 9, 10, 2, item_stack_pristine(ITEM_TWIGS, 3));
    items_add(&items, 10, 10, 3, item_stack_pristine(ITEM_BRANCHES, 2));
    tree_clear(&map, &items, 10, 10, 0);
    for (int z = 0; z < 3; z++) {
        CHECK(!is_trunk(&map, 10, 10, z));
    }
    CHECK(map_is_passable(&map, 10, 10, 0));
    CHECK(map_is_open(&map, 9, 10, 2));
    CHECK(map_is_open(&map, 10, 10, 3));
    CHECK(is_foliage(&map, 11, 10, 1)); /* The other tree's too: it stays. */
    CHECK(is_foliage(&map, 11, 10, 3));
    CHECK(is_trunk(&map, 12, 10, 1));
    /* What lay up there fell to the ground. */
    CHECK_EQ_INT(items_get(&items, 9, 10, 2).count, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_TWIGS], 3);
    CHECK_EQ_INT((int)items.totals[ITEM_BRANCHES], 2);
    CHECK_EQ_INT(items_get(&items, 9, 10, 0).type, ITEM_TWIGS);
    CHECK_EQ_INT(items_get(&items, 10, 10, 0).type, ITEM_BRANCHES);
    items_free(&items);
    map_free(&map);
}

static void test_aim(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    CHECK_EQ_INT(tree_fall_length(4), 4 * TREE_FALL_TILES_PER_LEVEL);
    CHECK_EQ_INT(tree_fall_length(0), 0);
    /* Nothing around: to the south, full length (4 levels). */
    TreeFall fall = tree_fall_aim(&map, 10, 10, 0);
    CHECK_EQ_INT(fall.direction, DIRECTION_SOUTH);
    CHECK_EQ_INT(fall.length, 8);
    CHECK_EQ_INT(fall.harmed, 0);
    /* A wall far down the line to the south: east instead. */
    map_set_structure(&map, 10, 17, 0, STRUCTURE_WALL);
    CHECK_EQ_INT(tree_fall_toward(&map, 10, 10, 0, DIRECTION_SOUTH).harmed, 1);
    CHECK_EQ_INT(tree_fall_aim(&map, 10, 10, 0).direction, DIRECTION_EAST);
    /* Rock close to the east: the trunk would lie short there; north is
     * clear and long. */
    map_set_rock(&map, 13, 10, 0, ROCK_GRANITE);
    CHECK_EQ_INT(tree_fall_toward(&map, 10, 10, 0, DIRECTION_EAST).length, 2);
    CHECK_EQ_INT(tree_fall_aim(&map, 10, 10, 0).direction, DIRECTION_NORTH);
    /* The edge of the map stops it too. */
    grow_tree(&map, 1, 1, 0, GROWN, 1);
    CHECK_EQ_INT(tree_fall_toward(&map, 1, 1, 0, DIRECTION_WEST).length, 1);
    /* A sapling does not fall. */
    put_tree(&map, 16, 3, 0, SAPLING);
    CHECK_EQ_INT(tree_fall_aim(&map, 16, 3, 0).length, 0);
    /* A wide trunk falls as a wide band: a wall beside its middle line is
     * hit. */
    Map wide;
    Items wide_items;
    REQUIRE(init_world(&wide, &wide_items));
    grow_tree(&wide, 4, 2, 0, GROWN, 2);
    map_set_structure(&wide, 5, 7, 0, STRUCTURE_WALL); /* Under its east half. */
    CHECK_EQ_INT(tree_fall_toward(&wide, 4, 2, 0, DIRECTION_SOUTH).harmed, 1);
    CHECK_EQ_INT(tree_fall_toward(&wide, 4, 2, 0, DIRECTION_EAST).harmed, 0);
    items_free(&wide_items);
    map_free(&wide);
    items_free(&items);
    map_free(&map);
}

static void test_fell(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 2, 0, GROWN, 1);
    Plant *plant = &map.plants[map_index(&map, 10, 2, 0)];
    int branches = tree_crown_yield(plant, ITEM_BRANCHES);
    int twigs = tree_crown_yield(plant, ITEM_TWIGS);
    CHECK_EQ_INT(branches, TREE_CROWN_BRANCHES);
    CHECK_EQ_INT(twigs, TREE_CROWN_TWIGS);
    ItemStack wood =
        item_stack_make(ITEM_WOOD, 24, ITEM_QUALITY_STANDARD, item_def(ITEM_WOOD)->max_hit_points);
    TreeFall fall = tree_fell(&map, &items, 10, 2, 0, wood);
    CHECK_EQ_INT(fall.direction, DIRECTION_SOUTH);
    CHECK_EQ_INT(fall.length, 8);
    CHECK_EQ_INT(map.plants[map_index(&map, 10, 2, 0)].type, PLANT_NONE);
    CHECK(!is_trunk(&map, 10, 2, 1));
    CHECK(map_is_open(&map, 11, 2, 1));
    /* The trunk on its foot and the first tiles, the crown at the far end. */
    CHECK_EQ_INT(count_on_line(&items, 10, 2, 0, 0, 1, 0, 6, ITEM_WOOD), 24);
    CHECK_EQ_INT(items_get(&items, 10, 2, 0).count, 4);
    CHECK_EQ_INT(count_on_line(&items, 10, 2, 0, 0, 1, 7, 8, ITEM_BRANCHES), branches);
    /* One kind of item per tile: the twigs beside the branches. */
    int near_crown = 0;
    for (int y = 8; y <= 12; y++) {
        for (int x = 9; x <= 11; x++) {
            ItemStack stack = items_get(&items, x, y, 0);
            near_crown += stack.type == ITEM_TWIGS ? stack.count : 0;
        }
    }
    CHECK_EQ_INT(near_crown, twigs);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 24);
    /* Nothing more where there is no tree. */
    fall = tree_fell(&map, &items, 10, 2, 0, wood);
    CHECK_EQ_INT(fall.length, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 24);
    /* A sapling is cut where it stands. */
    put_tree(&map, 3, 3, 0, SAPLING);
    fall = tree_fell(&map, &items, 3, 3, 0, item_stack_pristine(ITEM_WOOD, 2));
    CHECK_EQ_INT(fall.length, 0);
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3, 0)].type, PLANT_NONE);
    CHECK_EQ_INT(items_get(&items, 3, 3, 0).count, 2);
    items_free(&items);
    map_free(&map);
}

/* The order to fell trees: on the tree's foot (any tile of its trunk points
 * there), carried out as tree_fell() says, its wood by the tree's size. */
static void test_chop_order(void)
{
    Map map;
    Items items;
    Designations designations;
    REQUIRE(init_world(&map, &items));
    REQUIRE(designations_init(&designations, SIZE, SIZE, DEPTH));
    grow_tree(&map, 4, 4, 0, GROWN, 1);
    grow_tree(&map, 12, 12, 0, GROWN, 2);
    put_tree(&map, 16, 4, 0, SAPLING);

    /* Nobody can fell yet (woodcutting and axes come later). */
    const DesignationDef *def = designation_def(DESIGNATION_CHOP);
    REQUIRE(def != NULL);
    CHECK_EQ_INT(def->work_type, WORK_WOODCUTTING);
    CHECK(!designation_doable_at(DESIGNATION_CHOP, 100.0));

    /* On a tree's foot or a sapling; the order's tile from any trunk tile. */
    CHECK(designation_can_apply(&map, 4, 4, 0, DESIGNATION_CHOP));
    CHECK(designation_can_apply(&map, 16, 4, 0, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, 5, 4, 0, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, 4, 4, 1, DESIGNATION_CHOP)); /* Trunk, not the foot. */
    CHECK(!designation_can_apply(&map, 4, 4, 0, DESIGNATION_MINE)); /* Wood is not mined. */
    int x = 13, y = 13, z = 2;
    designation_target(&map, &x, &y, &z);
    CHECK(x == 12 && y == 12 && z == 0);
    x = 5, y = 4, z = 2; /* Foliage: stays. */
    REQUIRE(is_foliage(&map, 5, 4, 2));
    designation_target(&map, &x, &y, &z);
    CHECK(x == 5 && y == 4 && z == 2);

    /* Marking the four trunk tiles of the wide tree marks its foot once. */
    CHECK_EQ_INT(designations_set_rect(&designations, &map, 12, 12, 13, 13, 1, DESIGNATION_CHOP),
                 1);
    CHECK_EQ_INT(designation_get(&designations, 12, 12, 0), DESIGNATION_CHOP);
    CHECK_EQ_INT(designation_get(&designations, 13, 13, 1), DESIGNATION_NONE);
    CHECK(designation_set(&designations, &map, 4, 4, 2, DESIGNATION_CHOP)); /* Top of trunk. */
    CHECK_EQ_INT(designation_get(&designations, 4, 4, 0), DESIGNATION_CHOP);
    CHECK(designation_set(&designations, &map, 16, 4, 0, DESIGNATION_CHOP));
    CHECK(!designation_set(&designations, &map, 8, 8, 0, DESIGNATION_CHOP)); /* No tree. */
    CHECK_EQ_INT(designations.count, 3);
    CHECK_EQ_INT(designations_count_for_work(&designations, WORK_TYPE_BIT(WORK_WOODCUTTING)), 3);

    /* Work and wood by the tree's size. */
    const Plant *small = &map.plants[map_index(&map, 4, 4, 0)];
    const Plant *wide = &map.plants[map_index(&map, 12, 12, 0)];
    float needed = designation_work_needed(&designations, &map, 4, 4, 0);
    CHECK_NEAR(needed, tree_fell_work(small), 1e-3);
    CHECK_NEAR(needed, TREE_FELL_BASE_WORK + TREE_FELL_GROWTH_WORK, 1e-3);
    CHECK_NEAR(designation_work_needed(&designations, &map, 12, 12, 0),
               TREE_FELL_BASE_WORK + 4.0f * TREE_FELL_GROWTH_WORK, 1e-3);
    int count;
    CHECK_EQ_INT(designation_yield(&designations, &map, 4, 4, 0, &count), ITEM_WOOD);
    CHECK_EQ_INT(count, tree_wood_yield(small));
    CHECK_EQ_INT(count, plant_def(PLANT_TREE)->cut_yield);
    CHECK_EQ_INT(designation_yield(&designations, &map, 12, 12, 0, &count), ITEM_WOOD);
    CHECK_EQ_INT(count, tree_wood_yield(wide));
    CHECK_EQ_INT(count, 4 * plant_def(PLANT_TREE)->cut_yield);

    /* Felled once the work is done: the tree falls with its wood. */
    CHECK(!designation_add_work(&designations, &map, &items, 4, 4, 0, needed * 0.5f, 75.0, NULL));
    CHECK(is_trunk(&map, 4, 4, 0));
    CHECK(designation_add_work(&designations, &map, &items, 4, 4, 0, needed * 0.5f, 75.0, NULL));
    CHECK_EQ_INT(map.plants[map_index(&map, 4, 4, 0)].type, PLANT_NONE);
    CHECK(!is_trunk(&map, 4, 4, 0) && !is_trunk(&map, 4, 4, 2));
    CHECK(!is_foliage(&map, 5, 4, 2));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], plant_def(PLANT_TREE)->cut_yield);
    CHECK_EQ_INT(designation_get(&designations, 4, 4, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(designations.harmful_tree_falls, 0u);

    /* Cancelled from a trunk tile too. */
    CHECK_EQ_INT(designations_set_rect(&designations, &map, 13, 13, 13, 13, 2, DESIGNATION_NONE),
                 1);
    CHECK_EQ_INT(designation_get(&designations, 12, 12, 0), DESIGNATION_NONE);

    /* A tree with no clear side falls onto a building: counted. */
    grow_tree(&map, 4, 4, 0, GROWN, 1);
    map_set_structure(&map, 4, 5, 0, STRUCTURE_WALL);
    map_set_structure(&map, 5, 4, 0, STRUCTURE_WALL);
    map_set_structure(&map, 4, 3, 0, STRUCTURE_WALL);
    map_set_structure(&map, 3, 4, 0, STRUCTURE_WALL);
    REQUIRE(designation_set(&designations, &map, 4, 4, 0, DESIGNATION_CHOP));
    CHECK(designation_add_work(&designations, &map, &items, 4, 4, 0, needed, 75.0, NULL));
    CHECK_EQ_INT(designations.harmful_tree_falls, 1u);
    designations_free(&designations);
    items_free(&items);
    map_free(&map);
}

/* A tree on the edge of a hill, aimed out over the drop: what lies over open
 * space falls to the ground below. Walls hit lose half their hit points. */
static void test_fell_over_drop_and_onto_walls(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < 10; x++) { /* The hill: x < 10, ground on level 1. */
            map.rock[map_index(&map, x, y, 0)] = ROCK_EARTH;
            map.terrain[map_index(&map, x, y, 1)] = TERRAIN_SOIL;
        }
    }
    map_topology_changed(&map);
    grow_tree(&map, 8, 5, 1, GROWN, 1);
    map_set_structure(&map, 8, 7, 1, STRUCTURE_WALL); /* South, north and west: walls. */
    map_set_structure(&map, 8, 2, 1, STRUCTURE_WALL);
    map_set_structure(&map, 5, 5, 1, STRUCTURE_WALL);
    ItemStack wood =
        item_stack_make(ITEM_WOOD, 14, ITEM_QUALITY_STANDARD, item_def(ITEM_WOOD)->max_hit_points);
    TreeFall fall = tree_fell(&map, &items, 8, 5, 1, wood);
    CHECK_EQ_INT(fall.direction, DIRECTION_EAST);
    CHECK_EQ_INT(fall.harmed, 0);
    /* Its foot and the next tile on the hill, the rest down on level 0. */
    CHECK(items_get(&items, 8, 5, 1).count > 0);
    CHECK(items_get(&items, 9, 5, 1).count > 0);
    CHECK(items_get(&items, 10, 5, 0).count > 0);
    CHECK_EQ_INT(items_get(&items, 10, 5, 1).count, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 14);

    /* Walls on every side now: it comes down on one, which loses half its
     * hit points (never all of them). */
    grow_tree(&map, 4, 10, 1, GROWN, 1);
    map_set_structure(&map, 4, 13, 1, STRUCTURE_WALL);
    map_set_structure(&map, 4, 7, 1, STRUCTURE_WALL);
    map_set_structure(&map, 1, 10, 1, STRUCTURE_WALL);
    map_set_structure(&map, 7, 10, 1, STRUCTURE_WALL);
    int full = map_structure_hit_points(&map, 4, 13, 1);
    fall = tree_fell(&map, &items, 4, 10, 1, (ItemStack){0});
    CHECK_EQ_INT(fall.direction, DIRECTION_SOUTH);
    CHECK_EQ_INT(fall.harmed, 1);
    CHECK_EQ_INT(map_structure_hit_points(&map, 4, 13, 1),
                 full - full * TREE_FALL_DAMAGE_PERCENT / 100);
    CHECK_EQ_INT(map_structure_hit_points(&map, 4, 7, 1), full);
    for (int i = 0; i < 2; i++) {
        grow_tree(&map, 4, 10, 1, GROWN, 1);
        tree_fell(&map, &items, 4, 10, 1, (ItemStack){0});
    }
    CHECK_EQ_INT(map_structure(&map, 4, 13, 1), STRUCTURE_WALL); /* Battered, still there. */
    CHECK(map_structure_hit_points(&map, 4, 13, 1) >= 1);
    items_free(&items);
    map_free(&map);
}

/* Branches and twigs lying on a tile (Map.branches, Map.twigs) of a map. */
static int litter_total(const Map *map, ItemType type)
{
    int total = 0;
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                total += map_litter(map, x, y, z, type);
            }
        }
    }
    return total;
}

/* Where a tree's litter lies: on the foliage or on the ground, at most
 * MAP_*_MAX of each on a tile, never on a trunk. */
static void test_shedding(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    int on_foliage = 0, on_ground = 0;
    for (uint32_t pick = 0; pick < 4000; pick++) {
        tree_shed(&map, 10, 10, 0, ITEM_TWIGS, pick * 2654435761u);
    }
    for (int z = 0; z < DEPTH; z++) {
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int twigs = map_litter(&map, x, y, z, ITEM_TWIGS);
                CHECK(twigs <= MAP_TWIGS_MAX);
                if (twigs > 0) {
                    CHECK(!is_trunk(&map, x, y, z));
                    CHECK(abs(x - 10) <= 1 && abs(y - 10) <= 1); /* Under the crown. */
                    on_foliage += is_foliage(&map, x, y, z);
                    on_ground += z == 0;
                }
            }
        }
    }
    CHECK(on_foliage > 0);
    CHECK(on_ground > 0);
    /* Tiles fill up: 8 ground tiles around the trunk, 8 on each of its 3
     * levels of foliage (and the top one over the trunk, 9). */
    CHECK(litter_total(&map, ITEM_TWIGS) > 8 * MAP_TWIGS_MAX);
    CHECK(litter_total(&map, ITEM_TWIGS) <= (8 + 8 + 8 + 9) * MAP_TWIGS_MAX);
    CHECK_EQ_INT(litter_total(&map, ITEM_BRANCHES), 0);
    /* A full tile takes no more. */
    int before = litter_total(&map, ITEM_TWIGS);
    for (uint32_t pick = 0; pick < 200; pick++) {
        tree_shed(&map, 10, 10, 0, ITEM_TWIGS, pick);
    }
    CHECK(litter_total(&map, ITEM_TWIGS) >= before);
    /* Not a tree, nor a sapling: nothing falls. */
    CHECK(!tree_shed(&map, 1, 1, 0, ITEM_TWIGS, 0));
    put_tree(&map, 15, 15, 0, SAPLING);
    CHECK(!tree_shed(&map, 15, 15, 0, ITEM_TWIGS, 0));
    items_free(&items);
    map_free(&map);
}

/* Young trees shed twigs only, half as often; grown trees shed both. */
static void test_shedding_by_age(void)
{
    Plant plant = {.type = PLANT_TREE, .growth = SAPLING};
    CHECK(tree_shed_chance(&plant, ITEM_TWIGS) == 0.0f);
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) == 0.0f);
    plant.growth = YOUNG;
    float young_twigs = tree_shed_chance(&plant, ITEM_TWIGS);
    CHECK(young_twigs > 0.0f);
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) == 0.0f);
    plant.growth = HALF_GROWN;
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) == 0.0f);
    plant.growth = GROWN;
    CHECK(tree_shed_chance(&plant, ITEM_TWIGS) == 2.0f * young_twigs);
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) > 0.0f);
    plant.type = PLANT_HAZEL;
    CHECK(tree_shed_chance(&plant, ITEM_TWIGS) == 0.0f);
    CHECK_EQ_INT(tree_crown_yield(&plant, ITEM_TWIGS), 0);
}

/* A new world has twigs and branches under its grown trees and twigs under
 * the young ones, and the same ones from the same seed. */
static void test_litter_in_a_new_world(void)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = 42;
    params.width = params.height = 64;
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    int grown = 0, young = 0, twigs_under_grown = 0, branches_under_young = 0;
    for (int z = 0; z <= map.top_ground_level; z++) {
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                const Plant *plant = &map.plants[map_index(&map, x, y, z)];
                TreeShape shape;
                if (!tree_shape(plant, &shape) || !is_trunk(&map, x, y, z)) {
                    continue;
                }
                bool is_grown = shape.size > 1 || shape.trunk == 3;
                grown += is_grown;
                young += !is_grown;
                for (int dy = -shape.reach; dy < shape.size + shape.reach; dy++) {
                    for (int dx = -shape.reach; dx < shape.size + shape.reach; dx++) {
                        int tx = x + dx, ty = y + dy;
                        if (!map_in_bounds(&map, tx, ty, z)) {
                            continue;
                        }
                        bool twigs = map_litter(&map, tx, ty, z, ITEM_TWIGS) > 0;
                        bool branches = map_litter(&map, tx, ty, z, ITEM_BRANCHES) > 0;
                        twigs_under_grown += is_grown && twigs;
                        branches_under_young += !is_grown && branches;
                    }
                }
            }
        }
    }
    REQUIRE(grown > 0);
    CHECK(young > 0);
    CHECK(twigs_under_grown > 0);
    CHECK(litter_total(&map, ITEM_BRANCHES) > 0);
    /* Branches near a young tree only come from a grown neighbour's crown. */
    CHECK(branches_under_young < litter_total(&map, ITEM_BRANCHES));
    uint64_t hash = map_hash(&map);
    map_free(&map);
    REQUIRE(worldgen_generate(&map, &params));
    CHECK(map_hash(&map) == hash);
    map_free(&map);
}

/* The litter of a felled tree's foliage falls to the ground (what does not
 * fit lies as items). */
static void test_felling_drops_the_litter_of_the_foliage(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    CHECK(map_add_litter(&map, 11, 10, 2, ITEM_TWIGS)); /* On the foliage. */
    CHECK(map_add_litter(&map, 11, 10, 2, ITEM_BRANCHES));
    for (int i = 0; i < MAP_TWIGS_MAX; i++) {
        map_add_litter(&map, 11, 10, 0, ITEM_TWIGS); /* The ground below is full. */
    }
    tree_clear(&map, &items, 10, 10, 0);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 2, ITEM_TWIGS), 0);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 2, ITEM_BRANCHES), 0);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 0, ITEM_BRANCHES), 1);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 0, ITEM_TWIGS), MAP_TWIGS_MAX);
    CHECK_EQ_INT((int)items.totals[ITEM_TWIGS], 1);
    items_free(&items);
    map_free(&map);
}

/* A zone on the ground gathers what lies on the ground, whatever hangs in the
 * foliage above: nobody has to climb. A zone drawn over the foliage gathers
 * what lies there. Felling still works from the foot. */
static void test_gathering_on_trees(void)
{
    Map map;
    Items items;
    GatherZones zones;
    REQUIRE(init_world(&map, &items));
    REQUIRE(gather_zones_init(&zones, SIZE, SIZE, DEPTH));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    grow_tree(&map, 3, 3, 0, GROWN, 1);
    for (int d = 0; d < DIRECTION_COUNT; d++) { /* Walls all round the second tree. */
        map_set_structure(&map, 3 + direction_dx((Direction)d) * 2,
                          3 + direction_dy((Direction)d) * 2, 0, STRUCTURE_WALL);
    }
    map_add_litter(&map, 11, 10, 1, ITEM_TWIGS); /* On the foliage. */
    map_add_litter(&map, 11, 10, 1, ITEM_TWIGS);
    map_add_litter(&map, 9, 10, 0, ITEM_TWIGS);    /* On the ground. */
    CHECK(gather_zone_can_cover(&map, 10, 10, 0)); /* The foot of the trunk. */
    CHECK(!gather_zone_can_cover(&map, 10, 10, 1));
    CHECK(gather_zone_can_cover(&map, 11, 10, 1)); /* Foliage. */
    /* A zone on the ground. */
    int zone = gather_zone_create(&zones, &map, 0, 0, SIZE - 1, SIZE - 1, 0);
    gather_zone_set_enabled(&zones, zone, GATHER_TWIGS, true);
    gather_zone_set_mode(&zones, zone, GATHER_TWIGS, GATHER_FOREVER);
    gather_zones_update(&zones, items.totals, 1.0f);
    CHECK_EQ_INT(gather_zone_at(&zones, 11, 10, 1), GATHER_ZONE_NONE);
    CHECK(gather_job_available(&zones, &map, 9, 10, 0, GATHER_JOB_TWIGS));
    CHECK(!gather_job_available(&zones, &map, 11, 10, 1, GATHER_JOB_TWIGS));
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 11, 10, 1, ~0u), GATHER_JOB_NONE);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 9, 10, 0, ~0u), GATHER_JOB_TWIGS);
    long available[GATHER_RESOURCE_COUNT];
    gather_zone_available(&zones, &map, zone, available);
    CHECK_EQ_INT((int)available[GATHER_TWIGS], 1); /* The foliage's are not counted. */
    CHECK(
        !gather_zone_add_work(&zones, &map, &items, 11, 10, 1, GATHER_JOB_TWIGS, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(map_litter(&map, 11, 10, 1, ITEM_TWIGS), 2);
    CHECK(gather_zone_add_work(&zones, &map, &items, 9, 10, 0, GATHER_JOB_TWIGS, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(items_get(&items, 9, 10, 0).type, ITEM_TWIGS);
    CHECK_EQ_INT(items_get(&items, 9, 10, 0).count, 1);
    CHECK_EQ_INT(map_litter(&map, 9, 10, 0, ITEM_TWIGS), 0);

    /* A zone over the foliage itself gets what lies there. */
    int upper = gather_zone_create(&zones, &map, 0, 0, SIZE - 1, SIZE - 1, 1);
    REQUIRE(upper != GATHER_ZONE_NONE);
    gather_zone_set_enabled(&zones, upper, GATHER_TWIGS, true);
    gather_zone_set_mode(&zones, upper, GATHER_TWIGS, GATHER_FOREVER);
    gather_zones_update(&zones, items.totals, 1.0f);
    CHECK_EQ_INT(gather_zone_at(&zones, 11, 10, 1), upper);
    CHECK(gather_job_available(&zones, &map, 11, 10, 1, GATHER_JOB_TWIGS));
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 11, 10, 1, ~0u), GATHER_JOB_TWIGS);
    int priority = gather_zone_get(&zones, upper)->priority;
    CHECK_EQ_INT(gather_tile_job_in(&zones, &map, 11, 10, 1, ~0u, priority + 1), GATHER_JOB_NONE);
    gather_zone_available(&zones, &map, upper, available);
    CHECK_EQ_INT((int)available[GATHER_TWIGS], 2);
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 11, 10, 1, GATHER_JOB_TWIGS, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(items_get(&items, 11, 10, 1).type, ITEM_TWIGS); /* Up there. */
    CHECK_EQ_INT(items_get(&items, 11, 10, 1).count, 2);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 1, ITEM_TWIGS), 0);
    CHECK(!gather_job_available(&zones, &map, 11, 10, 1, GATHER_JOB_TWIGS));

    gather_zone_set_enabled(&zones, zone, GATHER_WOOD, true);
    gather_zone_set_mode(&zones, zone, GATHER_WOOD, GATHER_FOREVER);
    gather_zone_set_ripe_only(&zones, zone, GATHER_WOOD, false);
    gather_zones_update(&zones, items.totals, 1.0f);
    CHECK(gather_job_available(&zones, &map, 10, 10, 0, GATHER_JOB_CHOP));
    CHECK(!gather_job_available(&zones, &map, 11, 10, 1, GATHER_JOB_CHOP));
    CHECK(gather_zone_add_work(&zones, &map, &items, 10, 10, 0, GATHER_JOB_CHOP, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(map.plants[map_index(&map, 10, 10, 0)].type, PLANT_NONE);
    CHECK(!is_trunk(&map, 10, 10, 0));
    CHECK_EQ_INT(items_get(&items, 10, 12, 0).type, ITEM_WOOD); /* Along the trunk. */
    /* The crown came down with its twigs (what was gathered is gone). */
    CHECK_EQ_INT((int)items.totals[ITEM_TWIGS], 3 + TREE_CROWN_TWIGS);
    CHECK_EQ_UINT(zones.harmful_tree_falls, 0);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CHOP, 1e6f, 60.0, NULL));
    CHECK_EQ_UINT(zones.harmful_tree_falls, 1);
    gather_zones_free(&zones);
    items_free(&items);
    map_free(&map);
}

/* A settler sent for twigs by a zone on the ground never climbs the tree
 * above them; with a zone over the foliage, it climbs and takes them there. */
static void test_settler_climbs_only_for_a_zone_on_the_foliage(void)
{
    for (int over_foliage = 0; over_foliage < 2; over_foliage++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        Map *map = &game.map;
        put_tree(map, 16, 16, 0, GROWN);
        game_debug_skip(&game, GAME_PLANT_UPDATE_INTERVAL); /* Takes room. */
        REQUIRE(is_trunk(map, 16, 16, 0));
        map_add_litter(map, 17, 16, 1, ITEM_TWIGS); /* On the foliage. */
        map_add_litter(map, 17, 16, 1, ITEM_TWIGS);
        EntityId id = game_spawn_colonist(&game, 10, 16, 0);
        Colonist *colonist = game_colonist(&game, id);
        REQUIRE(colonist != NULL);
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        int zone =
            gather_zone_create(&game.gather_zones, map, 14, 14, 18, 18, over_foliage ? 1 : 0);
        REQUIRE(zone != GATHER_ZONE_NONE);
        gather_zone_set_enabled(&game.gather_zones, zone, GATHER_TWIGS, true);
        gather_zone_set_mode(&game.gather_zones, zone, GATHER_TWIGS, GATHER_FOREVER);
        bool climbed = false;
        for (int tick = 0; tick < 4 * 3600; tick++) {
            game_tick(&game);
            climbed = climbed || colonist->z > 0;
        }
        CHECK_EQ_INT(climbed, over_foliage);
        CHECK_EQ_INT(map_litter(map, 17, 16, 1, ITEM_TWIGS) == 0, over_foliage);
        game_shutdown(&game);
    }
}

/* In the running game, trees shed twigs and branches over the hours. */
static void test_trees_shed_in_the_game(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    put_tree(map, 16, 16, 0, GROWN);
    game_debug_skip(&game, GAME_PLANT_UPDATE_INTERVAL); /* Takes room. */
    REQUIRE(is_trunk(map, 16, 16, 0));
    CHECK_EQ_INT(litter_total(map, ITEM_TWIGS), 0);
    game_debug_skip(&game, 10 * 24 * GAME_PLANT_UPDATE_INTERVAL);
    CHECK(litter_total(map, ITEM_TWIGS) > 0);
    CHECK(litter_total(map, ITEM_BRANCHES) > 0);
    game_shutdown(&game);
    /* A young tree: twigs only. */
    REQUIRE(init_flat_game(&game));
    map = &game.map;
    put_tree(map, 16, 16, 0, YOUNG);
    game_debug_skip(&game, GAME_PLANT_UPDATE_INTERVAL);
    REQUIRE(is_trunk(map, 16, 16, 0));
    game_debug_skip(&game, 6 * 24 * GAME_PLANT_UPDATE_INTERVAL); /* Still young. */
    CHECK(litter_total(map, ITEM_TWIGS) > 0);
    CHECK_EQ_INT(litter_total(map, ITEM_BRANCHES), 0);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_shapes);
    RUN_TEST(test_build_and_find);
    RUN_TEST(test_climbing);
    RUN_TEST(test_room_to_grow);
    RUN_TEST(test_growing_in_the_game);
    RUN_TEST(test_clear_and_shared_foliage);
    RUN_TEST(test_aim);
    RUN_TEST(test_fell);
    RUN_TEST(test_fell_over_drop_and_onto_walls);
    RUN_TEST(test_chop_order);
    RUN_TEST(test_shedding);
    RUN_TEST(test_shedding_by_age);
    RUN_TEST(test_litter_in_a_new_world);
    RUN_TEST(test_felling_drops_the_litter_of_the_foliage);
    RUN_TEST(test_gathering_on_trees);
    RUN_TEST(test_settler_climbs_only_for_a_zone_on_the_foliage);
    RUN_TEST(test_trees_shed_in_the_game);
    return test_summary();
}
