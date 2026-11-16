#include "core/region.h"
#include "core/worldgen.h"

#include "test_framework.h"

#include <stdlib.h>

/* 16 x 16 soil map split in two by a rock wall at x = 6, with deep water in
 * the top left corner. */
static bool init_split_map(Map *map)
{
    if (!map_init(map, 16, 16, 1)) {
        return false;
    }
    for (int y = 0; y < 16; y++) {
        map_set_rock(map, 6, y, 0, ROCK_GRANITE);
    }
    map_set_terrain(map, 0, 0, 0, TERRAIN_DEEP_WATER);
    return true;
}

static void test_regions_follow_walls(void)
{
    Map map;
    Regions regions;
    CHECK(!regions_init(&regions, 0, 5, 1));
    REQUIRE(init_split_map(&map));
    REQUIRE(regions_init(&regions, 16, 16, 1));

    /* Not built yet: nothing is known, everything is assumed connected. */
    CHECK(!regions_current(&regions, &map));
    CHECK_EQ_UINT(regions_at(&regions, &map, 2, 2, 0), REGION_NONE);
    CHECK(regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){9, 2, 0}));

    regions_update(&regions, &map);
    CHECK(regions_current(&regions, &map));
    CHECK_EQ_UINT(regions.count, 2);
    uint32_t left = regions_at(&regions, &map, 2, 2, 0);
    uint32_t right = regions_at(&regions, &map, 9, 2, 0);
    CHECK(left != REGION_NONE && right != REGION_NONE && left != right);
    CHECK_EQ_UINT(regions_at(&regions, &map, 5, 7, 0), left);
    CHECK_EQ_UINT(regions_at(&regions, &map, 11, 0, 0), right);
    CHECK_EQ_UINT(regions_at(&regions, &map, 6, 3, 0), REGION_NONE); /* Wall. */
    CHECK_EQ_UINT(regions_at(&regions, &map, 0, 0, 0), REGION_NONE); /* Deep water. */
    CHECK_EQ_UINT(regions_at(&regions, &map, -1, 3, 0), REGION_NONE);
    CHECK_EQ_UINT(regions_at(&regions, &map, 3, 16, 0), REGION_NONE);

    CHECK(regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){5, 7, 0}));
    CHECK(!regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){9, 2, 0}));
    /* An impassable end is not a known "no": the path search settles it. */
    CHECK(regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){6, 3, 0}));

    regions_free(&regions);
    map_free(&map);
}

/* A hill: earth under a floor on level 1 over x >= 8, plains on level 0
 * elsewhere (open space above them). */
static bool init_hill_map(Map *map)
{
    if (!map_init(map, 16, 16, 2)) {
        return false;
    }
    for (int y = 0; y < 16; y++) {
        for (int x = 8; x < 16; x++) {
            map_set_rock(map, x, y, 0, ROCK_EARTH);
            map_set_terrain(map, x, y, 1, TERRAIN_SOIL);
        }
    }
    return true;
}

/* Levels are linked by slopes only. */
static void test_regions_across_levels(void)
{
    Map map;
    Regions regions;
    REQUIRE(init_hill_map(&map));
    REQUIRE(regions_init(&regions, 16, 16, 2));
    map_set_terrain(&map, 12, 12, 1, TERRAIN_SHALLOW_WATER);
    regions_update(&regions, &map);
    CHECK(regions_current(&regions, &map));
    CHECK_EQ_UINT(regions.count, 2); /* The plains and the hilltop: a cliff between. */
    uint32_t plains = regions_at(&regions, &map, 2, 2, 0);
    uint32_t hilltop = regions_at(&regions, &map, 12, 2, 1);
    CHECK(plains != REGION_NONE && hilltop != REGION_NONE && plains != hilltop);
    CHECK_EQ_UINT(regions_at(&regions, &map, 2, 2, 1), REGION_NONE);  /* Open space. */
    CHECK_EQ_UINT(regions_at(&regions, &map, 12, 2, 0), REGION_NONE); /* Earth. */
    CHECK(regions_has_drinking_spot(&regions, &map, hilltop));
    CHECK(!regions_has_drinking_spot(&regions, &map, plains));

    /* A slope up the side of the hill joins them. */
    map_set_slope(&map, 7, 5, 0, true);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.count, 1);
    CHECK(regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){12, 2, 1}));
    CHECK(regions_has_drinking_spot(&regions, &map, regions_at(&regions, &map, 2, 2, 0)));

    /* A gallery dug into the hill from the plains joins their region in place. */
    uint32_t rebuilds = regions.rebuilds;
    map_set_rock(&map, 8, 10, 0, ROCK_NONE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, rebuilds);
    CHECK_EQ_UINT(regions_at(&regions, &map, 8, 10, 0), regions_at(&regions, &map, 2, 2, 0));

    /* Rock over the slope closes it: the hilltop is cut off again. */
    map_set_rock(&map, 7, 5, 1, ROCK_GRANITE);
    regions_update(&regions, &map);
    CHECK(!regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){12, 2, 1}));
    regions_free(&regions);
    map_free(&map);
}

static void test_regions_notice_map_changes(void)
{
    Map map;
    Regions regions;
    REQUIRE(init_split_map(&map));
    REQUIRE(regions_init(&regions, 16, 16, 1));
    regions_update(&regions, &map);
    REQUIRE(!regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){9, 2, 0}));

    /* A passage is dug: until the update, the stale data must not say "no". */
    map_set_rock(&map, 6, 4, 0, ROCK_NONE);
    CHECK(!regions_current(&regions, &map));
    CHECK(regions_connected(&regions, &map, (TilePos){2, 2, 0}, (TilePos){9, 2, 0}));
    CHECK(regions_has_drinking_spot(&regions, &map, 1));

    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.count, 1);
    CHECK_EQ_UINT(regions_at(&regions, &map, 2, 2, 0), regions_at(&regions, &map, 9, 2, 0));

    /* Closed again, and a third area walled in. */
    map_set_rock(&map, 6, 4, 0, ROCK_SANDSTONE);
    map_set_rock(&map, 14, 15, 0, ROCK_GRANITE);
    map_set_rock(&map, 15, 14, 0, ROCK_GRANITE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.count, 3);
    CHECK(!regions_connected(&regions, &map, (TilePos){9, 2, 0}, (TilePos){15, 15, 0}));

    /* Plants never block: they do not change the topology. */
    uint32_t revision = map.topology_revision;
    map.plants[map_index(&map, 3, 3, 0)].type = PLANT_TREE;
    map.plants[map_index(&map, 3, 3, 0)].growth = 1.0f;
    CHECK(map_is_passable(&map, 3, 3, 0));
    CHECK_EQ_UINT(map.topology_revision, revision);
    for (int plant = 0; plant < PLANT_COUNT; plant++) {
        CHECK(plant_def((PlantType)plant)->move_cost > 0.0f);
    }

    regions_free(&regions);
    map_free(&map);
}

/* Region ids are arbitrary: two sets of regions agree when they group tiles
 * the same way and know the same drinking spots. */
static bool same_partition(const Regions *a, const Regions *b, const Map *map)
{
    size_t tiles = map_tile_count(map);
    for (size_t i = 0; i < tiles; i++) {
        if ((a->id[i] == REGION_NONE) != (b->id[i] == REGION_NONE)) {
            return false;
        }
        if (a->id[i] != REGION_NONE && a->drinkable[a->id[i]] != b->drinkable[b->id[i]]) {
            return false;
        }
        /* Comparing each tile with its right and bottom neighbors is enough. */
        size_t right = i + 1, below = i + (size_t)map->width;
        if (right % (size_t)map->width != 0 &&
            (a->id[i] == a->id[right]) != (b->id[i] == b->id[right])) {
            return false;
        }
        if (below < tiles && (a->id[i] == a->id[below]) != (b->id[i] == b->id[below])) {
            return false;
        }
    }
    return true;
}

static void test_dug_tiles_are_added_without_rebuilding(void)
{
    Map map;
    Regions regions, fresh;
    REQUIRE(init_split_map(&map));
    for (int y = 6; y <= 10; y++) {
        for (int x = 9; x <= 13; x++) {
            map_set_rock(&map, x, y, 0, ROCK_GRANITE); /* A solid block on the right side. */
        }
    }
    REQUIRE(regions_init(&regions, 16, 16, 1));
    REQUIRE(regions_init(&fresh, 16, 16, 1));
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 1);
    CHECK_EQ_UINT(regions.count, 2);

    /* Digging into the block from its edge: the tile joins the region next to it,
     * and its box. */
    map_set_rock(&map, 9, 8, 0, ROCK_NONE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 1);
    CHECK_EQ_UINT(regions_at(&regions, &map, 9, 8, 0), regions_at(&regions, &map, 8, 8, 0));
    TileBox joined = regions_box(&regions, &map, regions_at(&regions, &map, 9, 8, 0));
    CHECK(tile_box_contains(&joined, 9, 8, 0));

    /* A pocket in the middle of the block, cut off from everything: a new region. */
    map_set_rock(&map, 11, 8, 0, ROCK_NONE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 1);
    CHECK_EQ_UINT(regions.count, 3);
    CHECK(!regions_connected(&regions, &map, (TilePos){11, 8, 0}, (TilePos){8, 8, 0}));
    TileBox pocket = regions_box(&regions, &map, regions_at(&regions, &map, 11, 8, 0));
    CHECK(tile_box_contains(&pocket, 11, 8, 0));
    CHECK(!tile_box_contains(&pocket, 10, 8, 0));
    CHECK(!tile_box_contains(&pocket, 11, 9, 0));

    /* Several tiles dug between two updates. */
    map_set_rock(&map, 13, 6, 0, ROCK_NONE);
    map_set_rock(&map, 13, 7, 0, ROCK_NONE);
    map_set_rock(&map, 9, 9, 0, ROCK_NONE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 1);
    regions_update(&fresh, &map);
    CHECK(same_partition(&regions, &fresh, &map));

    /* Joining the pocket to the outside merges two regions: rebuilt. */
    map_set_rock(&map, 10, 8, 0, ROCK_NONE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 2);
    CHECK_EQ_UINT(regions.count, 2);
    CHECK(regions_connected(&regions, &map, (TilePos){11, 8, 0}, (TilePos){8, 8, 0}));

    /* Breaking through the wall between both sides as well. */
    map_set_rock(&map, 6, 3, 0, ROCK_NONE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 3);
    CHECK_EQ_UINT(regions.count, 1);

    /* Changing the kind of rock is not a change of topology. */
    uint32_t revision = map.topology_revision;
    map_set_rock(&map, 12, 9, 0, ROCK_SANDSTONE);
    map_set_rock(&map, 2, 2, 0, ROCK_NONE);
    CHECK_EQ_UINT(map.topology_revision, revision);

    /* Closing a tile, a terrain change, or more changes than the map
     * remembers: rebuilt. */
    map_set_rock(&map, 6, 3, 0, ROCK_GRANITE);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 4);
    map_set_terrain(&map, 3, 3, 0, TERRAIN_SAND);
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 5);
    for (int x = 0; x <= MAP_TOPOLOGY_LOG_SIZE; x++) {
        map_set_rock(&map, x % 16, 15, 0, ROCK_GRANITE);
        map_set_rock(&map, x % 16, 15, 0, ROCK_NONE);
    }
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.rebuilds, 6);
    regions_update(&fresh, &map);
    CHECK(same_partition(&regions, &fresh, &map));

    regions_free(&fresh);
    regions_free(&regions);
    map_free(&map);
}

/* Random digging: updating step by step always agrees with building from scratch. */
static void test_incremental_updates_match_rebuilds(void)
{
    Map map;
    Regions regions, fresh;
    REQUIRE(map_init(&map, 24, 24, 1));
    REQUIRE(regions_init(&regions, 24, 24, 1));
    REQUIRE(regions_init(&fresh, 24, 24, 1));
    uint32_t state = 2024;
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 24; x++) {
            state = state * 1664525u + 1013904223u;
            if ((state >> 24) % 3 != 0) {
                map_set_rock(&map, x, y, 0, ROCK_GRANITE);
            }
        }
    }
    map_set_terrain(&map, 12, 12, 0, TERRAIN_SHALLOW_WATER);
    regions_update(&regions, &map);

    bool agree = true;
    for (int step = 0; step < 300 && agree; step++) {
        state = state * 1664525u + 1013904223u;
        int digs = 1 + (int)((state >> 28) % 3);
        for (int i = 0; i < digs; i++) {
            state = state * 1664525u + 1013904223u;
            map_set_rock(&map, (int)((state >> 8) % 24), (int)((state >> 16) % 24), 0, ROCK_NONE);
        }
        regions_update(&regions, &map);
        fresh.built = false; /* From scratch. */
        regions_update(&fresh, &map);
        agree = regions_current(&regions, &map) && same_partition(&regions, &fresh, &map);
    }
    CHECK(agree);
    CHECK(regions.rebuilds < fresh.rebuilds); /* Most steps did not need one. */
    regions_free(&fresh);
    regions_free(&regions);
    map_free(&map);
}

static void test_diagonal_gaps_do_not_connect(void)
{
    /* Two rock tiles touching by a corner: walking cannot squeeze between. */
    Map map;
    Regions regions;
    REQUIRE(map_init(&map, 16, 16, 1));
    REQUIRE(regions_init(&regions, 16, 16, 1));
    for (int i = 0; i < 16; i++) {
        map_set_rock(&map, i, 15 - i, 0, ROCK_GRANITE); /* Anti-diagonal wall. */
    }
    regions_update(&regions, &map);
    CHECK_EQ_UINT(regions.count, 2);
    CHECK(!regions_connected(&regions, &map, (TilePos){0, 0, 0}, (TilePos){15, 15, 0}));

    /* The pathfinder agrees. */
    Pathfinder finder;
    REQUIRE(pathfinder_init(&finder, 16, 16, 1));
    CHECK(!pathfinder_find(&finder, &map, (TilePos){0, 0, 0}, (TilePos){15, 15, 0},
                           PATH_NO_COST_LIMIT, NULL));
    pathfinder_free(&finder);
    regions_free(&regions);
    map_free(&map);
}

static void test_drinking_spots_per_region(void)
{
    Map map;
    Regions regions;
    REQUIRE(init_split_map(&map));
    REQUIRE(regions_init(&regions, 16, 16, 1));
    regions_update(&regions, &map);
    uint32_t left = regions_at(&regions, &map, 2, 2, 0);
    uint32_t right = regions_at(&regions, &map, 9, 2, 0);
    /* The left side touches the deep water in the corner: its shore is a drinking spot. */
    CHECK(regions_has_drinking_spot(&regions, &map, left));
    CHECK(!regions_has_drinking_spot(&regions, &map, right));
    CHECK(regions_has_drinking_spot(&regions, &map, REGION_NONE)); /* Unknown. */
    CHECK(regions_has_drinking_spot(&regions, &map, 99));

    map_set_terrain(&map, 10, 3, 0, TERRAIN_SHALLOW_WATER);
    regions_update(&regions, &map);
    CHECK(regions_has_drinking_spot(&regions, &map, regions_at(&regions, &map, 9, 2, 0)));
    regions_free(&regions);
    map_free(&map);
}

static bool never(const Map *map, int x, int y, int z, void *userdata)
{
    (void)z;
    (void)map;
    (void)x;
    (void)y;
    (void)userdata;
    return false; /* The search floods everything reachable. */
}

/* On generated maps, regions agree with what the pathfinder can reach. */
static void test_regions_match_pathfinding_on_generated_maps(void)
{
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params;
        worldgen_default_params(&params);
        params.seed = seed;
        params.width = 64;
        params.height = 64;
        Map map;
        Regions regions;
        Pathfinder finder;
        REQUIRE(worldgen_generate(&map, &params));
        REQUIRE(regions_init(&regions, 64, 64, map.depth));
        REQUIRE(pathfinder_init(&finder, 64, 64, map.depth));
        regions_update(&regions, &map);

        size_t sizes[8] = {0};
        for (size_t i = 0; i < map_tile_count(&map); i++) {
            CHECK((regions.id[i] != REGION_NONE) == (map_move_cost_at(&map, i) > 0.0f));
            if (regions.id[i] != REGION_NONE) { /* Every tile inside its region's box. */
                int x, y, z;
                map_tile_of(&map, i, &x, &y, &z);
                TileBox box = regions_box(&regions, &map, regions.id[i]);
                CHECK(tile_box_contains(&box, x, y, z));
            }
            if (regions.id[i] != REGION_NONE && regions.id[i] <= 8) {
                sizes[regions.id[i] - 1]++;
            }
        }
        /* The area reachable from one tile of each of the first regions is the region. */
        bool checked[8] = {false};
        for (size_t i = 0; i < map_tile_count(&map); i++) {
            uint32_t region = regions.id[i];
            if (region == REGION_NONE || region > 8 || checked[region - 1]) {
                continue;
            }
            checked[region - 1] = true;
            int x, y, z;
            map_tile_of(&map, i, &x, &y, &z);
            pathfinder_find_nearest(&finder, &map, (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                    never, NULL, PATH_NO_COST_LIMIT, NULL, NULL);
            CHECK_EQ_UINT(finder.last_expanded, sizes[region - 1]);
        }
        /* Slopes join the hills and the shores to the plains: a region on several
         * levels. */
        uint8_t *on_plains = calloc((size_t)regions.count + 1, 1);
        REQUIRE(on_plains != NULL);
        bool spans_levels = false;
        size_t plains = (size_t)map.surface_level * map_level_tile_count(&map);
        for (size_t i = plains; i < plains + map_level_tile_count(&map); i++) {
            on_plains[regions.id[i]] = 1;
        }
        for (size_t i = 0; i < map_tile_count(&map); i++) {
            if ((i < plains || i >= plains + map_level_tile_count(&map)) &&
                regions.id[i] != REGION_NONE && on_plains[regions.id[i]]) {
                spans_levels = true;
            }
        }
        CHECK(spans_levels);
        free(on_plains);
        pathfinder_free(&finder);
        regions_free(&regions);
        map_free(&map);
    }
}

/* Without up-to-date regions, or for no region, the box is the whole map. */
static void test_unknown_region_box_is_the_map(void)
{
    Map map;
    Regions regions;
    REQUIRE(init_split_map(&map));
    REQUIRE(regions_init(&regions, 16, 16, 1));
    TileBox box = regions_box(&regions, &map, 1); /* Not built yet. */
    CHECK(box.x0 == 0 && box.y0 == 0 && box.z0 == 0);
    CHECK(box.x1 == 15 && box.y1 == 15 && box.z1 == 0);
    regions_update(&regions, &map);
    box = regions_box(&regions, &map, REGION_NONE);
    CHECK(box.x1 == 15 && box.y1 == 15);
    map_set_rock(&map, 3, 3, 0, ROCK_GRANITE); /* Stale until the next update. */
    box = regions_box(&regions, &map, 1);
    CHECK(box.x0 == 0 && box.x1 == 15);
    regions_free(&regions);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_regions_follow_walls);
    RUN_TEST(test_regions_across_levels);
    RUN_TEST(test_regions_notice_map_changes);
    RUN_TEST(test_dug_tiles_are_added_without_rebuilding);
    RUN_TEST(test_incremental_updates_match_rebuilds);
    RUN_TEST(test_diagonal_gaps_do_not_connect);
    RUN_TEST(test_drinking_spots_per_region);
    RUN_TEST(test_regions_match_pathfinding_on_generated_maps);
    RUN_TEST(test_unknown_region_box_is_the_map);
    return test_summary();
}
