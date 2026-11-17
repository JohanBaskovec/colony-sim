#include "core/pathfinding.h"
#include "core/worldgen.h"

#include "test_framework.h"

#define SQRT2 1.41421356

static TilePos tile(int x, int y)
{
    return (TilePos){(int16_t)x, (int16_t)y, 0};
}

/* Map of plain soil: every tile costs 1. */
static bool make_open_map(Map *map, Pathfinder *finder, int size)
{
    return map_init(map, size, size, 1) && pathfinder_init(finder, size, size, 1);
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
        map->rock[map_index(map, x, y, 0)] = ROCK_GRANITE;
    }
}

/* Every step is adjacent to the previous one (or up or down a slope from
 * it, or straight up or down a tree), passable, and the path ends at
 * `goal`. */
static void check_path_is_valid(const Map *map, TilePos start, TilePos goal, const Path *path)
{
    TilePos previous = start;
    for (int i = 0; i < path->count; i++) {
        TilePos step = path->steps[i];
        CHECK(abs(step.x - previous.x) <= 1 && abs(step.y - previous.y) <= 1);
        CHECK(map_is_passable(map, step.x, step.y, step.z));
        if (step.x == previous.x && step.y == previous.y) {
            CHECK(abs(step.z - previous.z) == 1);
            CHECK(
                map_can_climb_tree(map, step.x, step.y, step.z < previous.z ? step.z : previous.z));
        } else if (step.z == previous.z + 1) {
            CHECK(map_can_climb(map, previous.x, previous.y, previous.z, step.x, step.y));
        } else if (step.z == previous.z - 1) {
            CHECK(map_can_climb(map, step.x, step.y, step.z, previous.x, previous.y));
        } else {
            CHECK_EQ_INT(step.z, previous.z);
        }
        previous = step;
    }
    CHECK(previous.x == goal.x && previous.y == goal.y && previous.z == goal.z);
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
    map.rock[map_index(&map, 5, 4, 0)] = ROCK_GRANITE;
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
            map.terrain[map_index(&map, x, y, 0)] = TERRAIN_SHALLOW_WATER;
        }
    }
    REQUIRE(pathfinder_find(&finder, &map, tile(5, 5), tile(18, 5), PATH_NO_COST_LIMIT, &path));
    double straight_cost = 10.0 + 3.0 * 3.0; /* Through the water. */
    CHECK(finder.last_cost < straight_cost);
    for (int i = 0; i < path.count; i++) {
        TilePos step = path.steps[i];
        CHECK(map.terrain[map_index(&map, step.x, step.y, step.z)] != TERRAIN_SHALLOW_WATER);
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

static bool has_tree(const Map *map, int x, int y, int z, void *userdata)
{
    (void)userdata;
    return map->plants[map_index(map, x, y, z)].type == PLANT_TREE;
}

static void test_find_nearest_matching_tile(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    TilePos found;
    REQUIRE(make_open_map(&map, &finder, 32));
    map.plants[map_index(&map, 20, 10, 0)].type = PLANT_TREE;
    map.plants[map_index(&map, 13, 10, 0)].type = PLANT_TREE;

    REQUIRE(pathfinder_find_nearest(&finder, &map, tile(10, 10), has_tree, NULL, PATH_NO_COST_LIMIT,
                                    &path, &found));
    CHECK_EQ_INT(found.x, 13);
    CHECK_EQ_INT(found.y, 10);
    check_path_is_valid(&map, tile(10, 10), found, &path);

    /* Out of range. */
    CHECK(
        !pathfinder_find_nearest(&finder, &map, tile(10, 10), has_tree, NULL, 2.0f, &path, &found));

    /* The start tile itself counts. */
    REQUIRE(pathfinder_find_nearest(&finder, &map, tile(13, 10), has_tree, NULL, PATH_NO_COST_LIMIT,
                                    &path, &found));
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

/* Plains on level 0 over x < 8; a hill over x >= 8: earth on level 0 under
 * a floor on level 1. */
static void make_hill(Map *map)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 8; x < map->width; x++) {
            map_set_rock(map, x, y, 0, ROCK_EARTH);
            map_set_terrain(map, x, y, 1, TERRAIN_SOIL);
        }
    }
}

/* Slopes lead up and down, one level at a time, orthogonally. */
/* Ladders lead up like slopes, at a slower MAP_LADDER_CLIMB_COST; a slope
 * wins over a ladder for the same detour. */
static void test_paths_climb_ladders(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(map_init(&map, 16, 16, 2));
    REQUIRE(pathfinder_init(&finder, 16, 16, 2));
    make_hill(&map);
    TilePos start = {2, 5, 0}, goal = {12, 5, 1};
    REQUIRE(map_ladder_fits(&map, 7, 9, 0));
    map_set_structure(&map, 7, 9, 0, STRUCTURE_LADDER);
    REQUIRE(pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    check_path_is_valid(&map, start, goal, &path);
    CHECK_NEAR(finder.last_cost, 1.0 + 8.0 * SQRT2 + MAP_LADDER_CLIMB_COST, 1e-3);
    REQUIRE(pathfinder_find(&finder, &map, goal, start, PATH_NO_COST_LIMIT, &path));
    check_path_is_valid(&map, goal, start, &path);
    CHECK_NEAR(finder.last_cost, 1.0 + 8.0 * SQRT2 + MAP_LADDER_CLIMB_COST, 1e-3);

    /* A slope on the same row as the ladder, one tile up: taken instead. */
    map_set_slope(&map, 7, 8, 0, true);
    REQUIRE(pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    CHECK(finder.last_cost < 1.0 + 8.0 * SQRT2 + MAP_LADDER_CLIMB_COST - 0.5);
    path_free(&path);
    pathfinder_free(&finder);
    map_free(&map);
}

/* Animals walk on four legs: slopes, but no ladder, and no door. */
static void test_animals_open_no_door_and_climb_no_ladder(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(map_init(&map, 16, 16, 2));
    REQUIRE(pathfinder_init(&finder, 16, 16, 2));
    CHECK_EQ_INT(finder.walker, PATH_WALKER_SETTLER);
    make_hill(&map);
    TilePos start = {2, 5, 0}, top = {12, 5, 1};
    map_set_structure(&map, 7, 9, 0, STRUCTURE_LADDER);
    REQUIRE(pathfinder_find(&finder, &map, start, top, PATH_NO_COST_LIMIT, &path));
    finder.walker = PATH_WALKER_ANIMAL;
    CHECK(!pathfinder_find(&finder, &map, start, top, PATH_NO_COST_LIMIT, &path));
    CHECK(!pathfinder_find(&finder, &map, top, start, PATH_NO_COST_LIMIT, &path));
    CHECK(path_walker_can_stand(PATH_WALKER_ANIMAL, &map, 7, 9, 0)); /* Under the ladder. */
    CHECK(path_walker_can_step(PATH_WALKER_SETTLER, &map, (TilePos){7, 9, 0}, (TilePos){8, 9, 1}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){7, 9, 0}, (TilePos){8, 9, 1}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){8, 9, 1}, (TilePos){7, 9, 0}));
    /* A slope: up and down like anyone. */
    map_set_slope(&map, 7, 4, 0, true);
    REQUIRE(pathfinder_find(&finder, &map, start, top, PATH_NO_COST_LIMIT, &path));
    check_path_is_valid(&map, start, top, &path);
    TilePos previous = start;
    for (int i = 0; i < path.count; i++) {
        CHECK(path_walker_can_step(PATH_WALKER_ANIMAL, &map, previous, path.steps[i]));
        if (path.steps[i].z != previous.z) {
            CHECK(previous.x == 7 && previous.y == 4);
        }
        previous = path.steps[i];
    }
    REQUIRE(pathfinder_find(&finder, &map, top, start, PATH_NO_COST_LIMIT, &path));
    CHECK(path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){8, 4, 1}, (TilePos){7, 4, 0}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){7, 4, 0}, (TilePos){7, 5, 1}));

    /* A wall of rock with a door in it, on the plain. */
    for (int y = 0; y < map.height; y++) {
        map_set_rock(&map, 4, y, 0, ROCK_GRANITE);
    }
    map_set_rock(&map, 4, 5, 0, ROCK_NONE);
    map_set_structure(&map, 4, 5, 0, STRUCTURE_DOOR);
    TilePos beyond = {6, 5, 0};
    finder.walker = PATH_WALKER_SETTLER;
    REQUIRE(pathfinder_find(&finder, &map, start, beyond, PATH_NO_COST_LIMIT, &path));
    finder.walker = PATH_WALKER_ANIMAL;
    CHECK(!pathfinder_find(&finder, &map, start, beyond, PATH_NO_COST_LIMIT, &path));
    CHECK(!pathfinder_find(&finder, &map, start, (TilePos){4, 5, 0}, PATH_NO_COST_LIMIT, &path));
    CHECK(pathfinder_reachable_count(&finder, &map, start, PATH_NO_COST_LIMIT) == 4u * 16u);
    CHECK(path_walker_can_stand(PATH_WALKER_SETTLER, &map, 4, 5, 0));
    CHECK(!path_walker_can_stand(PATH_WALKER_ANIMAL, &map, 4, 5, 0));
    CHECK(!path_walker_can_stand(PATH_WALKER_ANIMAL, &map, 4, 6, 0)); /* Rock. */
    CHECK(!path_walker_can_stand(PATH_WALKER_ANIMAL, &map, -1, 6, 0));
    CHECK(path_walker_can_step(PATH_WALKER_SETTLER, &map, (TilePos){3, 5, 0}, (TilePos){4, 5, 0}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){3, 5, 0}, (TilePos){4, 5, 0}));
    /* Steps: a neighbor, never round the corner of a wall, never far. */
    CHECK(path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){2, 5, 0}, (TilePos){3, 6, 0}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){2, 5, 0}, (TilePos){2, 5, 0}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){2, 5, 0}, (TilePos){2, 7, 0}));
    map_set_rock(&map, 2, 8, 0, ROCK_GRANITE);
    CHECK(!path_walker_can_step(PATH_WALKER_SETTLER, &map, (TilePos){1, 8, 0}, (TilePos){2, 9, 0}));
    CHECK(path_walker_can_step(PATH_WALKER_SETTLER, &map, (TilePos){1, 9, 0}, (TilePos){2, 9, 0}));
    path_free(&path);
    pathfinder_free(&finder);
    map_free(&map);
}

static void test_paths_climb_slopes(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    TilePos found;
    REQUIRE(map_init(&map, 16, 16, 2));
    REQUIRE(pathfinder_init(&finder, 16, 16, 2));
    make_hill(&map);
    TilePos start = {2, 5, 0}, goal = {12, 5, 1};
    CHECK(!pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path)); /* A cliff. */
    CHECK(!pathfinder_find(&finder, &map, start, (TilePos){2, 5, 1}, PATH_NO_COST_LIMIT,
                           &path)); /* Open space. */

    map_set_slope(&map, 7, 9, 0, true);
    REQUIRE(pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    check_path_is_valid(&map, start, goal, &path);
    CHECK(path.steps[path.count - 1].z == 1);
    /* Up to the slope (1 straight and 4 diagonal moves), the climb, then 4
     * diagonal moves on the hilltop. */
    CHECK_NEAR(finder.last_cost, 1.0 + 8.0 * SQRT2 + MAP_CLIMB_COST, 1e-3);
    TilePos previous = start;
    for (int i = 0; i < path.count; i++) {
        TilePos step = path.steps[i];
        if (step.z != previous.z) { /* The climb: orthogonal, from the slope. */
            CHECK(previous.x == 7 && previous.y == 9 && previous.z == 0);
            CHECK(step.x == 8 && step.y == 9 && step.z == 1);
        }
        previous = step;
    }
    /* Down again, the same way. */
    REQUIRE(pathfinder_find(&finder, &map, goal, start, PATH_NO_COST_LIMIT, &path));
    check_path_is_valid(&map, goal, start, &path);
    CHECK_NEAR(finder.last_cost, 1.0 + 8.0 * SQRT2 + MAP_CLIMB_COST, 1e-3);

    /* The nearest tree, down on the plains. */
    map.plants[map_index(&map, 3, 9, 0)].type = PLANT_TREE;
    REQUIRE(pathfinder_find_nearest(&finder, &map, goal, has_tree, NULL, PATH_NO_COST_LIMIT, &path,
                                    &found));
    CHECK(found.x == 3 && found.y == 9 && found.z == 0);
    /* The plains and the hilltop: 128 tiles each. */
    CHECK_EQ_UINT(pathfinder_reachable_count(&finder, &map, start, PATH_NO_COST_LIMIT), 256);

    /* Rock over the slope: no way up. */
    map_set_rock(&map, 7, 9, 1, ROCK_GRANITE);
    CHECK(!pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_UINT(pathfinder_reachable_count(&finder, &map, start, PATH_NO_COST_LIMIT), 128);
    cleanup(&map, &finder, &path);
}

static bool is_tile(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const TilePos *goal = userdata;
    return x == goal->x && y == goal->y && z == goal->z;
}

/* On hilly generated maps, A* finds paths as cheap as a plain Dijkstra search
 * (its heuristic never overestimates, climbs included). */
static void test_shortest_paths_over_hills(void)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = 3;
    params.width = 64;
    params.height = 64;
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(worldgen_generate(&map, &params));
    REQUIRE(pathfinder_init(&finder, 64, 64, map.depth));
    int compared = 0, climbing = 0;
    for (int i = 0; i < 400 && compared < 60; i++) {
        TilePos start = {(int16_t)(i * 7 % 64), (int16_t)(i * 13 % 64), 0};
        TilePos goal = {(int16_t)(i * 29 % 64), (int16_t)(i * 31 % 64), 0};
        start.z = (int16_t)map_ground_level(&map, start.x, start.y);
        goal.z = (int16_t)map_ground_level(&map, goal.x, goal.y);
        if (!pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path)) {
            continue;
        }
        check_path_is_valid(&map, start, goal, &path);
        float cost = finder.last_cost;
        REQUIRE(pathfinder_find_nearest(&finder, &map, start, is_tile, &goal, PATH_NO_COST_LIMIT,
                                        NULL, NULL));
        CHECK_NEAR(cost, finder.last_cost, 1e-3);
        compared++;
        climbing += start.z != goal.z;
    }
    CHECK(compared >= 30);
    CHECK(climbing > 0);
    cleanup(&map, &finder, &path);
}

/* Passable tiles of a box whose (x + y) is a multiple of 5: a scattered set
 * of targets, as the tiles of a zone. */
static bool is_scattered_in_box(const Map *map, int x, int y, int z, void *userdata)
{
    const TileBox *box = userdata;
    return tile_box_contains(box, x, y, z) && (x + y) % 5 == 0 && map_is_passable(map, x, y, z);
}

/* Searching for the nearest tile of a box with the box as a guide finds one as
 * cheap to reach as the plain search does, over hills too, exploring less when
 * the box is far. */
static void test_nearest_in_box_is_as_cheap(void)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = 3;
    params.width = 64;
    params.height = 64;
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(worldgen_generate(&map, &params));
    REQUIRE(pathfinder_init(&finder, 64, 64, map.depth));
    int compared = 0, fewer = 0;
    for (int i = 0; i < 200 && compared < 60; i++) {
        TilePos start = {(int16_t)(i * 7 % 64), (int16_t)(i * 13 % 64), 0};
        start.z = (int16_t)map_ground_level(&map, start.x, start.y);
        int bx = i * 29 % 56, by = i * 31 % 56;
        TileBox box = {bx, by, 0, bx + 7, by + 7, map.depth - 1};
        if (i % 3 == 0) {
            box.z0 = box.z1 = map_ground_level(&map, bx, by); /* One level only. */
        }
        TilePos plain_found, guided_found;
        bool plain = pathfinder_find_nearest(&finder, &map, start, is_scattered_in_box, &box,
                                             PATH_NO_COST_LIMIT, NULL, &plain_found);
        float plain_cost = finder.last_cost;
        size_t plain_expanded = finder.last_expanded;
        bool guided =
            pathfinder_find_nearest_in_box(&finder, &map, start, &box, is_scattered_in_box, &box,
                                           PATH_NO_COST_LIMIT, &path, &guided_found);
        CHECK_EQ_INT(plain, guided);
        if (!plain || !guided) {
            continue;
        }
        CHECK_NEAR(plain_cost, finder.last_cost, 1e-3);
        CHECK(is_scattered_in_box(&map, guided_found.x, guided_found.y, guided_found.z, &box));
        check_path_is_valid(&map, start, guided_found, &path);
        fewer += finder.last_expanded < plain_expanded;
        compared++;
    }
    CHECK(compared >= 30);
    CHECK(fewer > compared / 2);
    /* Within a cost limit: both give up alike. */
    TilePos corner = {0, 0, (int16_t)map_ground_level(&map, 0, 0)};
    TileBox far = {56, 56, 0, 63, 63, map.depth - 1};
    CHECK(!pathfinder_find_nearest_in_box(&finder, &map, corner, &far, is_scattered_in_box, &far,
                                          10.0f, NULL, NULL));
    TileBox empty = tile_box_empty();
    CHECK(!pathfinder_find_nearest_in_box(&finder, &map, corner, &empty, is_scattered_in_box,
                                          &empty, PATH_NO_COST_LIMIT, NULL, NULL));
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

/* Sideways wins on a diagonal; a step straight up or down a level keeps the heading. */
static void test_step_heading(void)
{
    TilePos here = {10, 10, 0};
    CHECK_EQ_INT(path_step_heading(here, (TilePos){10, 11, 0}, HEADING_EAST), HEADING_SOUTH);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){10, 9, 0}, HEADING_EAST), HEADING_NORTH);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){11, 10, 0}, HEADING_SOUTH), HEADING_EAST);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){9, 10, 0}, HEADING_SOUTH), HEADING_WEST);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){11, 11, 0}, HEADING_SOUTH), HEADING_EAST);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){9, 9, 0}, HEADING_SOUTH), HEADING_WEST);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){10, 9, 1}, HEADING_SOUTH), HEADING_NORTH);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){10, 10, 1}, HEADING_WEST), HEADING_WEST);
    CHECK_EQ_INT(path_step_heading(here, (TilePos){10, 10, 1}, HEADING_NORTH), HEADING_NORTH);
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
    RUN_TEST(test_paths_climb_slopes);
    RUN_TEST(test_paths_climb_ladders);
    RUN_TEST(test_animals_open_no_door_and_climb_no_ladder);
    RUN_TEST(test_shortest_paths_over_hills);
    RUN_TEST(test_nearest_in_box_is_as_cheap);
    RUN_TEST(test_many_searches_reuse_memory);
    RUN_TEST(test_step_heading);
    return test_summary();
}
