#include "core/pathfinding.h"

#include "test_framework.h"

#define SQRT2 1.41421356

static TilePos tile(int x, int y)
{
    return (TilePos){(int16_t)x, (int16_t)y};
}

/* Map of plain soil: every tile costs 1. */
static bool make_open_map(Map *map, Pathfinder *finder, int size)
{
    return map_init(map, size, size) && pathfinder_init(finder, size, size);
}

static void cleanup(Map *map, Pathfinder *finder, Path *path)
{
    path_free(path);
    pathfinder_free(finder);
    map_free(map);
}

static void wall_column(Map *map, int x, int y0, int y1)
{
    for (int y = y0; y <= y1; y++) {
        map->rock[map_index(map, x, y)] = ROCK_GRANITE;
    }
}

/* Every step is adjacent to the previous one, passable, and the path ends at `goal`. */
static void check_path_is_valid(const Map *map, TilePos start, TilePos goal, const Path *path)
{
    TilePos previous = start;
    for (int i = 0; i < path->count; i++) {
        TilePos step = path->steps[i];
        CHECK(abs(step.x - previous.x) <= 1 && abs(step.y - previous.y) <= 1);
        CHECK(!(step.x == previous.x && step.y == previous.y));
        CHECK(map_is_passable(map, step.x, step.y));
        previous = step;
    }
    CHECK(previous.x == goal.x && previous.y == goal.y);
}

static void test_straight_and_diagonal_paths(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 32));

    REQUIRE(pathfinder_find(&finder, &map, tile(2, 5), tile(10, 5), PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_INT(path.count, 8);
    CHECK_NEAR(finder.last_cost, 8.0, 1e-4);
    check_path_is_valid(&map, tile(2, 5), tile(10, 5), &path);

    REQUIRE(pathfinder_find(&finder, &map, tile(0, 0), tile(6, 6), PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_INT(path.count, 6);
    CHECK_NEAR(finder.last_cost, 6.0 * SQRT2, 1e-3);

    /* Mixed: 3 diagonal + 4 straight moves. */
    REQUIRE(pathfinder_find(&finder, &map, tile(0, 0), tile(7, 3), PATH_NO_COST_LIMIT, &path));
    CHECK_NEAR(finder.last_cost, 4.0 + 3.0 * SQRT2, 1e-3);
    cleanup(&map, &finder, &path);
}

static void test_start_equals_goal_gives_empty_path(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 16));
    CHECK(pathfinder_find(&finder, &map, tile(4, 4), tile(4, 4), PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_INT(path.count, 0);
    cleanup(&map, &finder, &path);
}

static void test_goes_around_walls(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 32));
    wall_column(&map, 10, 0, 20); /* Gap below y = 20. */

    REQUIRE(pathfinder_find(&finder, &map, tile(5, 5), tile(15, 5), PATH_NO_COST_LIMIT, &path));
    check_path_is_valid(&map, tile(5, 5), tile(15, 5), &path);
    bool went_below_wall = false;
    for (int i = 0; i < path.count; i++) {
        went_below_wall |= path.steps[i].y > 20;
    }
    CHECK(went_below_wall);
    cleanup(&map, &finder, &path);
}

static void test_unreachable_and_invalid_goals(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 16));
    wall_column(&map, 8, 0, 15); /* Splits the map in two. */

    path.count = 0;
    CHECK(!pathfinder_find(&finder, &map, tile(2, 2), tile(12, 2), PATH_NO_COST_LIMIT, &path));
    CHECK(!pathfinder_find(&finder, &map, tile(2, 2), tile(8, 2), PATH_NO_COST_LIMIT, &path));
    CHECK(!pathfinder_find(&finder, &map, tile(2, 2), tile(-1, 2), PATH_NO_COST_LIMIT, &path));
    CHECK(!pathfinder_find(&finder, &map, tile(-1, 2), tile(2, 2), PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_INT(path.count, 0); /* Untouched on failure. */
    cleanup(&map, &finder, &path);
}

static void test_no_corner_cutting(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 16));
    /* Rock at (5,4): going from (4,4) to (5,5) diagonally would cut its corner. */
    map.rock[map_index(&map, 5, 4)] = ROCK_GRANITE;
    REQUIRE(pathfinder_find(&finder, &map, tile(4, 4), tile(5, 5), PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_INT(path.count, 2);
    CHECK_NEAR(finder.last_cost, 2.0, 1e-4);
    cleanup(&map, &finder, &path);
}

static void test_prefers_cheaper_terrain(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 32));
    /* A band of shallow water (cost 3) across the straight line, with a dry detour. */
    for (int y = 0; y < 8; y++) {
        for (int x = 10; x < 13; x++) {
            map.terrain[map_index(&map, x, y)] = TERRAIN_SHALLOW_WATER;
        }
    }
    REQUIRE(pathfinder_find(&finder, &map, tile(5, 5), tile(18, 5), PATH_NO_COST_LIMIT, &path));
    double straight_cost = 10.0 + 3.0 * 3.0; /* Through the water. */
    CHECK(finder.last_cost < straight_cost);
    for (int i = 0; i < path.count; i++) {
        TilePos step = path.steps[i];
        CHECK(map.terrain[map_index(&map, step.x, step.y)] != TERRAIN_SHALLOW_WATER);
    }
    cleanup(&map, &finder, &path);
}

static void test_cost_limit(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 32));
    CHECK(!pathfinder_find(&finder, &map, tile(0, 0), tile(20, 0), 10.0f, &path));
    CHECK(pathfinder_find(&finder, &map, tile(0, 0), tile(20, 0), 20.0f, &path));
    cleanup(&map, &finder, &path);
}

static bool has_tree(const Map *map, int x, int y, void *userdata)
{
    (void)userdata;
    return map->plants[map_index(map, x, y)].type == PLANT_TREE;
}

static void test_find_nearest_matching_tile(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    TilePos found;
    REQUIRE(make_open_map(&map, &finder, 32));
    map.plants[map_index(&map, 20, 10)].type = PLANT_TREE;
    map.plants[map_index(&map, 13, 10)].type = PLANT_TREE;

    REQUIRE(pathfinder_find_nearest(&finder, &map, tile(10, 10), has_tree, NULL,
                                    PATH_NO_COST_LIMIT, &path, &found));
    CHECK_EQ_INT(found.x, 13);
    CHECK_EQ_INT(found.y, 10);
    check_path_is_valid(&map, tile(10, 10), found, &path);

    /* Out of range. */
    CHECK(!pathfinder_find_nearest(&finder, &map, tile(10, 10), has_tree, NULL, 2.0f, &path,
                                   &found));

    /* The start tile itself counts. */
    REQUIRE(pathfinder_find_nearest(&finder, &map, tile(13, 10), has_tree, NULL,
                                    PATH_NO_COST_LIMIT, &path, &found));
    CHECK_EQ_INT(path.count, 0);
    CHECK_EQ_INT(found.x, 13);
    cleanup(&map, &finder, &path);
}

static void test_reachable_count(void)
{
    Map map;
    Pathfinder finder;
    REQUIRE(make_open_map(&map, &finder, 16));
    CHECK_EQ_UINT(pathfinder_reachable_count(&finder, &map, tile(0, 0), PATH_NO_COST_LIMIT), 256);
    wall_column(&map, 4, 0, 15);
    CHECK_EQ_UINT(pathfinder_reachable_count(&finder, &map, tile(0, 0), PATH_NO_COST_LIMIT), 64);
    CHECK_EQ_UINT(pathfinder_reachable_count(&finder, &map, tile(0, 0), 0.0f), 1);
    Path path = {0};
    cleanup(&map, &finder, &path);
}

static void test_many_searches_reuse_memory(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(make_open_map(&map, &finder, 64));
    for (int i = 0; i < 500; i++) {
        TilePos start = tile(i % 64, (i * 7) % 64);
        TilePos goal = tile((i * 13) % 64, (i * 29) % 64);
        CHECK(pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    }
    cleanup(&map, &finder, &path);
}

int main(void)
{
    RUN_TEST(test_straight_and_diagonal_paths);
    RUN_TEST(test_start_equals_goal_gives_empty_path);
    RUN_TEST(test_goes_around_walls);
    RUN_TEST(test_unreachable_and_invalid_goals);
    RUN_TEST(test_no_corner_cutting);
    RUN_TEST(test_prefers_cheaper_terrain);
    RUN_TEST(test_cost_limit);
    RUN_TEST(test_find_nearest_matching_tile);
    RUN_TEST(test_reachable_count);
    RUN_TEST(test_many_searches_reuse_memory);
    return test_summary();
}
