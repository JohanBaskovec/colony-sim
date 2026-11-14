#include "core/worldgen.h"

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
            CHECK(map.rock[i] == ROCK_NONE);
            CHECK(fertility >= plant_def((PlantType)plant->type)->min_fertility);
            CHECK(plant->growth > 0.0f && plant->growth <= 1.0f);
        }
        if (map.rock[i] != ROCK_NONE) {
            CHECK_EQ_INT(map.terrain[i], TERRAIN_ROUGH_STONE);
        }
    }
    map_free(&map);
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
                passable += map_is_passable(&map, x, y);
            }
        }
        double ratio = (double)passable / (double)map_tile_count(&map);
        if (ratio < 0.5) {
            fprintf(stderr, "seed %llu: only %.0f%% passable\n", (unsigned long long)seed,
                    ratio * 100.0);
        }
        CHECK(ratio >= 0.5);
        CHECK(map_count_plants(&map, PLANT_TREE) > 0);
        map_free(&map);
    }
}

int main(void)
{
    RUN_TEST(test_same_seed_gives_identical_map);
    RUN_TEST(test_different_seeds_give_different_maps);
    RUN_TEST(test_invalid_size_fails);
    RUN_TEST(test_tiles_are_valid_and_consistent);
    RUN_TEST(test_maps_are_mostly_usable_land);
    return test_summary();
}
