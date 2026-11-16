#include "core/worldgen.h"

#include "core/tree.h"

#include "test_framework.h"

static WorldGenParams params_for_seed(uint64_t seed)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = seed;
    params.width = 128;
    params.height = 128;
    return params;
}

static void test_same_seed_gives_identical_map(void)
{
    WorldGenParams params = params_for_seed(12345);
    Map a, b;
    REQUIRE(worldgen_generate(&a, &params));
    REQUIRE(worldgen_generate(&b, &params));
    CHECK_EQ_UINT(map_hash(&a), map_hash(&b));
    map_free(&a);
    map_free(&b);
}

static void test_different_seeds_give_different_maps(void)
{
    WorldGenParams p1 = params_for_seed(1);
    WorldGenParams p2 = params_for_seed(2);
    Map a, b;
    REQUIRE(worldgen_generate(&a, &p1));
    REQUIRE(worldgen_generate(&b, &p2));
    CHECK(map_hash(&a) != map_hash(&b));
    map_free(&a);
    map_free(&b);
}

static void test_invalid_size_fails(void)
{
    WorldGenParams params = params_for_seed(1);
    params.width = 4;
    Map map;
    CHECK(!worldgen_generate(&map, &params));
}

static void test_tiles_are_valid_and_consistent(void)
{
    WorldGenParams params = params_for_seed(777);
    Map map;
    REQUIRE(worldgen_generate(&map, &params));

    for (size_t i = 0; i < map_tile_count(&map); i++) {
        CHECK(map.terrain[i] < TERRAIN_COUNT);
        CHECK(map.rock[i] < ROCK_COUNT);
        CHECK(map.plants[i].type < PLANT_COUNT);

        const Plant *plant = &map.plants[i];
        if (plant->type != PLANT_NONE) {
            float fertility = terrain_def((Terrain)map.terrain[i])->fertility;
            /* A tree's foot is under its trunk (core/tree.h). */
            CHECK(map.rock[i] == ROCK_NONE ||
                  (plant->type == PLANT_TREE && map.rock[i] == ROCK_WOOD));
            CHECK(fertility >= plant_def((PlantType)plant->type)->min_fertility);
            /* Withered plants (wild garlic at the end of summer) wait at 0. */
            CHECK(plant->growth > 0.0f || plant_is_dormant(plant));
            CHECK(plant->growth <= 1.0f);
        }
        /* A floor under the rock, should it be dug out. */
        if (map.rock[i] == ROCK_EARTH) {
            CHECK_EQ_INT(map.terrain[i], TERRAIN_SOIL);
        } else if (map.rock[i] != ROCK_NONE && map.rock[i] != ROCK_WOOD) {
            CHECK_EQ_INT(map.terrain[i], TERRAIN_ROUGH_STONE);
        }
    }
    map_free(&map);
}

/* Stones: none on sand or water, 0 to MAP_STONES_MAX on stony ground, with
 * every count from 0 to the most turning up, and flint on some tiles only. */
static void test_stones(void)
{
    long tiles = 0, flint_tiles = 0;
    long seen[MAP_STONES_MAX + 1] = {0};
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int z = map_ground_level(&map, x, y);
                int cobbles = map_stones(&map, x, y, z, ITEM_COBBLE);
                int flint = map_stones(&map, x, y, z, ITEM_FLINT);
                Terrain type = (Terrain)map.terrain[map_index(&map, x, y, z)];
                const TerrainDef *terrain = terrain_def(type);
                if (!terrain->stony) {
                    CHECK_EQ_INT(cobbles + flint, 0);
                    continue;
                }
                CHECK(cobbles + flint <= MAP_STONES_MAX);
                seen[cobbles + flint]++;
                tiles++;
                flint_tiles += flint > 0;
            }
        }
        map_free(&map);
    }
    for (int count = 0; count <= MAP_STONES_MAX; count++) {
        CHECK(seen[count] > 0);
    }
    CHECK(flint_tiles > 0);
    CHECK(flint_tiles < tiles / 4);
    CHECK(!terrain_def(TERRAIN_SAND)->stony);
    CHECK(terrain_def(TERRAIN_GRAVEL)->stony);
}

/* Sanity check of the overall composition over several seeds. */
static void test_maps_are_mostly_usable_land(void)
{
    for (uint64_t seed = 1; seed <= 8; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));

        size_t passable = 0;
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                passable += map_is_passable(&map, x, y, map_ground_level(&map, x, y));
            }
        }
        double ratio = (double)passable / (double)map_level_tile_count(&map);
        if (ratio < 0.5) {
            fprintf(stderr, "seed %llu: only %.0f%% passable\n", (unsigned long long)seed,
                    ratio * 100.0);
        }
        CHECK(ratio >= 0.5);
        CHECK(map_count_plants(&map, PLANT_TREE) > 0);
        map_free(&map);
    }
}

/* Every column: rock up to its ground, a floor on the ground, open space
 * above. Plains on the surface level, lakes and rivers in hollows below it
 * (shallows and beaches one level down, deep water two), solid ground under
 * them, hills above; a few levels in all. */
static void test_relief(void)
{
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        const int surface = map.surface_level;
        CHECK_EQ_INT(surface, WORLDGEN_UNDERGROUND_LEVELS);
        CHECK(map.depth > surface + 2);
        CHECK(map.depth <= surface + 8 + TREE_MAX_LEVELS); /* Hills, not mountains. */
        CHECK_EQ_INT(map.depth, map.top_ground_level + TREE_MAX_LEVELS);
        size_t levels[MAP_MAX_LEVELS] = {0};
        size_t water = 0;
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int ground = map_ground_level(&map, x, y);
                levels[ground]++;
                CHECK(ground >= surface - 2); /* Solid deeper down. */
                /* Open air over it, for the tallest tree (and a roof). */
                CHECK(ground <= map.top_ground_level);
                for (int z = 0; z < map.depth; z++) {
                    size_t i = map_index(&map, x, y, z);
                    if (z < ground) {
                        CHECK(map.rock[i] != ROCK_NONE);
                        if (ground - z > WORLDGEN_EARTH_LEVELS) { /* Rock deep down. */
                            CHECK(map.rock[i] != ROCK_EARTH);
                        }
                    } else if (z == ground) {
                        CHECK(map_has_floor(&map, x, y, z));
                        CHECK(map.rock[i] == ROCK_NONE || map.rock[i] == ROCK_WOOD); /* A trunk. */
                    } else {
                        TreeRef tree; /* Open air, but for the trees. */
                        CHECK(map_is_open(&map, x, y, z) || tree_at(&map, x, y, z, &tree));
                        CHECK(map.plants[i].type == PLANT_NONE);
                    }
                }
                Terrain terrain = (Terrain)map.terrain[map_index(&map, x, y, ground)];
                if (terrain == TERRAIN_DEEP_WATER) {
                    CHECK_EQ_INT(ground, surface - 2);
                    water++;
                } else if (terrain == TERRAIN_SHALLOW_WATER) {
                    CHECK_EQ_INT(ground, surface - 1);
                    water++;
                }
                /* Rough stone on top of rock outcrops, rock inside them. */
                if (ground > surface && terrain == TERRAIN_ROUGH_STONE) {
                    RockType inside = (RockType)map.rock[map_index(&map, x, y, ground - 1)];
                    CHECK(inside == ROCK_GRANITE || inside == ROCK_SANDSTONE);
                }
            }
        }
        CHECK(water > 0);
        /* Lowlands: plains and the water in their hollows. */
        CHECK(levels[surface] + levels[surface - 1] + levels[surface - 2] >
              map_level_tile_count(&map) / 4);
        CHECK(levels[surface] > map_level_tile_count(&map) / 8);
        CHECK(levels[surface + 1] > 0 && levels[surface + 2] > 0);
        map_free(&map);
    }
}

/* Slopes lead up onto soft hills and out of the shallows; rock outcrops and
 * deep water stand behind cliffs. */
static void test_slopes(void)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    WorldGenParams params = params_for_seed(3);
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    CHECK(map.slope_count > 0);
    int counted = 0, wading = 0;
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        if (map.slope[i] == 0) {
            continue;
        }
        counted++;
        int x, y, z;
        map_tile_of(&map, i, &x, &y, &z);
        CHECK_EQ_INT(z, map_ground_level(&map, x, y));
        bool leads_up = false;
        for (int d = 0; d < 4; d++) {
            leads_up |= map_can_climb(&map, x, y, z, x + DX[d], y + DY[d]);
        }
        CHECK(leads_up);
        Terrain terrain = (Terrain)map.terrain[i];
        CHECK(terrain != TERRAIN_DEEP_WATER);
        wading += terrain == TERRAIN_SHALLOW_WATER;
    }
    CHECK_EQ_INT(counted, map.slope_count);
    CHECK(wading > 0); /* Shallows slope up their banks. */
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            int z = map_ground_level(&map, x, y);
            if (z <= map.surface_level ||
                map.terrain[map_index(&map, x, y, z)] != TERRAIN_ROUGH_STONE) {
                continue;
            }
            for (int d = 0; d < 4; d++) { /* Nothing climbs onto an outcrop. */
                CHECK(!map_can_climb(&map, x + DX[d], y + DY[d], z - 1, x, y));
            }
        }
    }
    map_free(&map);
}

/* Flat worlds: one level of ground, rock outcrops as rock walls on it, open
 * air over it (room for trees and roofs). */
static void test_flat(void)
{
    WorldGenParams params = params_for_seed(3);
    params.flat = true;
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    CHECK_EQ_INT(map.depth, TREE_MAX_LEVELS);
    CHECK_EQ_INT(map.surface_level, 0);
    CHECK_EQ_INT(map.top_ground_level, 0);
    TreeRef tree;
    for (int z = 1; z < map.depth; z++) { /* Open air, but for the trees. */
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                CHECK(map_is_open(&map, x, y, z) || tree_at(&map, x, y, z, &tree));
            }
        }
    }
    CHECK_EQ_INT(map.slope_count, 0);
    size_t rock = 0;
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        rock += map.rock[i] != ROCK_NONE && map.rock[i] != ROCK_WOOD;
        CHECK(map.rock[i] == ROCK_NONE || map.rock[i] == ROCK_WOOD ||
              map.terrain[i] == TERRAIN_ROUGH_STONE);
    }
    CHECK(rock > 0);
    map_free(&map);
}

/* Trees stand apart and are written into the map: every tree past its
 * sapling stages has its trunk, every tile of trunk or foliage belongs to a
 * tree; old and ancient trees turn up, rarely. */
static void test_trees(void)
{
    long trees = 0, wide = 0;
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        for (int z = 0; z < map.depth; z++) {
            for (int y = 0; y < map.height; y++) {
                for (int x = 0; x < map.width; x++) {
                    size_t i = map_index(&map, x, y, z);
                    TreeRef tree;
                    if (map.rock[i] == ROCK_WOOD ||
                        (map.rock[i] == ROCK_NONE && map.terrain[i] == TERRAIN_FOLIAGE)) {
                        CHECK(tree_at(&map, x, y, z, &tree));
                    }
                    const Plant *plant = &map.plants[i];
                    if (plant->type != PLANT_TREE) {
                        continue;
                    }
                    trees++;
                    wide += plant->size > 1;
                    TreeShape shape;
                    CHECK_EQ_INT(map.rock[i], tree_shape(plant, &shape) ? ROCK_WOOD : ROCK_NONE);
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            bool other = (dx != 0 || dy != 0) &&
                                         map_in_bounds(&map, x + dx, y + dy, z) &&
                                         map.plants[map_index(&map, x + dx, y + dy, z)].type ==
                                             PLANT_TREE;
                            CHECK(!other);
                        }
                    }
                }
            }
        }
        map_free(&map);
    }
    CHECK(trees > 0);
    CHECK(wide > 0);
    CHECK(wide < trees / 20);
}

int main(void)
{
    RUN_TEST(test_same_seed_gives_identical_map);
    RUN_TEST(test_different_seeds_give_different_maps);
    RUN_TEST(test_invalid_size_fails);
    RUN_TEST(test_tiles_are_valid_and_consistent);
    RUN_TEST(test_maps_are_mostly_usable_land);
    RUN_TEST(test_stones);
    RUN_TEST(test_relief);
    RUN_TEST(test_slopes);
    RUN_TEST(test_flat);
    RUN_TEST(test_trees);
    return test_summary();
}
