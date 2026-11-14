#include "core/i18n.h"
#include "core/map.h"
#include "core/pathfinding.h"

#include "test_framework.h"

static void test_init_rejects_invalid_sizes(void)
{
    Map map;
    CHECK(!map_init(&map, MAP_MIN_SIZE - 1, 32));
    CHECK(!map_init(&map, 32, MAP_MAX_SIZE + 1));
    CHECK(map.terrain == NULL && map.width == 0);
}

static void test_init_fills_with_soil(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 20));
    CHECK_EQ_INT(map.width, 32);
    CHECK_EQ_INT(map.height, 20);
    CHECK_EQ_UINT(map_tile_count(&map), 640);
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        CHECK_EQ_INT(map.terrain[i], TERRAIN_SOIL);
        CHECK_EQ_INT(map.rock[i], ROCK_NONE);
        CHECK_EQ_INT(map.plants[i].type, PLANT_NONE);
    }
    map_free(&map);
    CHECK(map.terrain == NULL);
}

static void test_bounds_and_index(void)
{
    Map map;
    REQUIRE(map_init(&map, 20, 16));
    CHECK(map_in_bounds(&map, 0, 0));
    CHECK(map_in_bounds(&map, 19, 15));
    CHECK(!map_in_bounds(&map, -1, 0));
    CHECK(!map_in_bounds(&map, 20, 0));
    CHECK(!map_in_bounds(&map, 0, 16));
    CHECK_EQ_UINT(map_index(&map, 0, 0), 0);
    CHECK_EQ_UINT(map_index(&map, 5, 2), 45);
    map_free(&map);
}

static void test_passability_and_move_cost(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16));

    CHECK(map_is_passable(&map, 1, 1));
    CHECK_NEAR(map_move_cost(&map, 1, 1), 1.0, 1e-6);
    CHECK(!map_is_passable(&map, -1, 0));

    map.terrain[map_index(&map, 2, 2)] = TERRAIN_DEEP_WATER;
    CHECK(!map_is_passable(&map, 2, 2));

    map.rock[map_index(&map, 3, 3)] = ROCK_GRANITE;
    CHECK(!map_is_passable(&map, 3, 3));

    map.plants[map_index(&map, 4, 4)].type = PLANT_TREE;
    CHECK_NEAR(map_move_cost(&map, 4, 4), plant_def(PLANT_TREE)->move_cost, 1e-6);

    map.terrain[map_index(&map, 5, 5)] = TERRAIN_SHALLOW_WATER;
    CHECK(map_move_cost(&map, 5, 5) > 1.0f);
    CHECK_NEAR(map_move_cost_at(&map, map_index(&map, 4, 4)), map_move_cost(&map, 4, 4), 1e-6);
    map_free(&map);
}

static void test_hash_detects_changes(void)
{
    Map a, b;
    REQUIRE(map_init(&a, 24, 24));
    REQUIRE(map_init(&b, 24, 24));
    CHECK_EQ_UINT(map_hash(&a), map_hash(&b));

    b.plants[10].type = PLANT_GRASS;
    CHECK(map_hash(&a) != map_hash(&b));
    a.plants[10].type = PLANT_GRASS;
    CHECK_EQ_UINT(map_hash(&a), map_hash(&b));

    b.plants[10].growth = 0.5f;
    CHECK(map_hash(&a) != map_hash(&b));
    map_free(&a);
    map_free(&b);

    REQUIRE(map_init(&a, 16, 32));
    REQUIRE(map_init(&b, 32, 16));
    CHECK(map_hash(&a) != map_hash(&b)); /* Same tile count, different shape. */
    map_free(&a);
    map_free(&b);
}

static void test_count_plants(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16));
    map.plants[0].type = PLANT_TREE;
    map.plants[1].type = PLANT_TREE;
    map.plants[2].type = PLANT_BUSH;
    CHECK_EQ_UINT(map_count_plants(&map, PLANT_TREE), 2);
    CHECK_EQ_UINT(map_count_plants(&map, PLANT_BUSH), 1);
    CHECK_EQ_UINT(map_count_plants(&map, PLANT_NONE), 253);
    map_free(&map);
}

static void test_definitions_are_complete(void)
{
    for (int t = 0; t < TERRAIN_COUNT; t++) {
        const TerrainDef *def = terrain_def((Terrain)t);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        /* Passable terrain never costs less than the pathfinding heuristic assumes. */
        CHECK(def->move_cost == 0.0f || def->move_cost >= PATH_MIN_TILE_COST);
    }
    for (int r = ROCK_NONE + 1; r < ROCK_COUNT; r++) {
        CHECK(i18n_is_valid_key(rock_def((RockType)r)->name_key));
        CHECK(rock_def((RockType)r)->mine_work > 0.0f);
        CHECK(item_def(rock_def((RockType)r)->mined_item) != NULL);
    }
    for (int p = PLANT_NONE + 1; p < PLANT_COUNT; p++) {
        const PlantDef *def = plant_def((PlantType)p);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(def->growth_per_day > 0.0f);
        CHECK(def->move_cost >= 1.0f);
        /* Either drops something when cut, or nothing at all. */
        CHECK((def->cut_item == ITEM_NONE) == (def->cut_yield == 0));
        CHECK((def->harvest_item == ITEM_NONE) == (def->harvest_yield == 0));
    }
    CHECK(terrain_def(TERRAIN_COUNT) == NULL);
    CHECK(rock_def(ROCK_COUNT) == NULL);
    CHECK(plant_def(PLANT_COUNT) == NULL);
}

static void test_edible_plants(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16));
    Plant *bush = &map.plants[map_index(&map, 3, 3)];
    bush->type = PLANT_BUSH;
    bush->growth = 1.0f;
    CHECK(map_has_edible_plant(&map, 3, 3));
    bush->growth = plant_def(PLANT_BUSH)->min_edible_growth - 0.01f;
    CHECK(!map_has_edible_plant(&map, 3, 3));

    Plant *tree = &map.plants[map_index(&map, 4, 4)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    CHECK(!map_has_edible_plant(&map, 4, 4));
    CHECK(!map_has_edible_plant(&map, 5, 5));
    CHECK(!map_has_edible_plant(&map, -1, 5));
    map_free(&map);
}

static void test_water_and_drinking_spots(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16));
    map.terrain[map_index(&map, 5, 5)] = TERRAIN_SHALLOW_WATER;
    map.terrain[map_index(&map, 10, 10)] = TERRAIN_DEEP_WATER;
    map.terrain[map_index(&map, 0, 0)] = TERRAIN_DEEP_WATER;

    CHECK(map_is_water(&map, 5, 5));
    CHECK(map_is_water(&map, 10, 10));
    CHECK(!map_is_water(&map, 6, 5));
    CHECK(!map_is_water(&map, -1, 5));

    CHECK(map_is_drinking_spot(&map, 5, 5)); /* Standing in shallow water. */
    CHECK(map_is_drinking_spot(&map, 6, 5)); /* Shore tiles, 4-neighborhood. */
    CHECK(map_is_drinking_spot(&map, 5, 4));
    CHECK(!map_is_drinking_spot(&map, 6, 6)); /* Diagonal only. */
    CHECK(!map_is_drinking_spot(&map, 10, 10)); /* Deep water is not walkable. */
    CHECK(map_is_drinking_spot(&map, 10, 11));
    CHECK(map_is_drinking_spot(&map, 1, 0)); /* Map edge does not break the lookup. */
    CHECK(!map_is_drinking_spot(&map, 3, 3));

    map.rock[map_index(&map, 11, 10)] = ROCK_GRANITE;
    CHECK(!map_is_drinking_spot(&map, 11, 10)); /* Rock walls are not standable. */
    map_free(&map);
}

static void test_plant_growth_stages_and_fruit(void)
{
    Plant plant = {.type = PLANT_TREE, .growth = 0.0f};
    CHECK_EQ_INT(plant_growth_stage(&plant), 0);
    plant.growth = 0.19f;
    CHECK_EQ_INT(plant_growth_stage(&plant), 0);
    plant.growth = 0.21f;
    CHECK_EQ_INT(plant_growth_stage(&plant), 1);
    plant.growth = 0.79f;
    CHECK_EQ_INT(plant_growth_stage(&plant), 3);
    plant.growth = 1.0f;
    CHECK_EQ_INT(plant_growth_stage(&plant), PLANT_GROWTH_STAGES - 1);
    plant.growth = 1.5f; /* Out of range: clamped. */
    CHECK_EQ_INT(plant_growth_stage(&plant), PLANT_GROWTH_STAGES - 1);
    CHECK(!plant_has_fruit(&plant)); /* Trees bear no fruit. */

    const PlantDef *bush = plant_def(PLANT_BUSH);
    plant = (Plant){.type = PLANT_BUSH, .growth = bush->min_edible_growth};
    CHECK(plant_has_fruit(&plant));
    plant.growth = PLANT_GROWTH_AFTER_PICKING; /* Just picked. */
    CHECK(!plant_has_fruit(&plant));
    plant = (Plant){.type = PLANT_COUNT, .growth = 1.0f};
    CHECK(!plant_has_fruit(&plant));
}

int main(void)
{
    RUN_TEST(test_init_rejects_invalid_sizes);
    RUN_TEST(test_init_fills_with_soil);
    RUN_TEST(test_bounds_and_index);
    RUN_TEST(test_passability_and_move_cost);
    RUN_TEST(test_hash_detects_changes);
    RUN_TEST(test_count_plants);
    RUN_TEST(test_definitions_are_complete);
    RUN_TEST(test_edible_plants);
    RUN_TEST(test_water_and_drinking_spots);
    RUN_TEST(test_plant_growth_stages_and_fruit);
    return test_summary();
}
