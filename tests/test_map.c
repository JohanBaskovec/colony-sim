#include "core/i18n.h"
#include "core/item.h"
#include "core/map.h"
#include "core/pathfinding.h"

#include "test_framework.h"

static void test_init_rejects_invalid_sizes(void)
{
    Map map;
    CHECK(!map_init(&map, MAP_MIN_SIZE - 1, 32, 1));
    CHECK(!map_init(&map, 32, MAP_MAX_SIZE + 1, 1));
    CHECK(map.terrain == NULL && map.width == 0);
}

static void test_init_fills_with_soil(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 20, 1));
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
    REQUIRE(map_init(&map, 20, 16, 1));
    CHECK(map_in_bounds(&map, 0, 0, 0));
    CHECK(map_in_bounds(&map, 19, 15, 0));
    CHECK(!map_in_bounds(&map, -1, 0, 0));
    CHECK(!map_in_bounds(&map, 20, 0, 0));
    CHECK(!map_in_bounds(&map, 0, 16, 0));
    CHECK_EQ_UINT(map_index(&map, 0, 0, 0), 0);
    CHECK_EQ_UINT(map_index(&map, 5, 2, 0), 45);
    map_free(&map);
}

/* A map of several levels: the layers hold them one after the other, and
 * every map function looks at one level only. */
/* Floors, open space, slopes and the ground level. */
static void test_floors_and_slopes(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 3));
    /* A fresh map: a floor of soil on level 0, open space above. */
    CHECK(map_has_floor(&map, 3, 3, 0));
    CHECK(!map_has_floor(&map, 3, 3, 1));
    CHECK(map_is_open(&map, 3, 3, 2));
    CHECK(!map_is_open(&map, 3, 3, 0));
    CHECK(!map_is_open(&map, 3, 3, 3)); /* Out of bounds. */
    CHECK(!map_is_passable(&map, 3, 3, 1)); /* Nothing to stand on. */
    CHECK_EQ_INT(map_ground_level(&map, 3, 3), 0);
    CHECK(!map_natural_roof(&map, 3, 3, 0));

    /* A hill over x >= 8: earth on level 0, a floor on level 1. */
    for (int y = 0; y < 16; y++) {
        for (int x = 8; x < 16; x++) {
            map_set_rock(&map, x, y, 0, ROCK_EARTH);
            map_set_terrain(&map, x, y, 1, TERRAIN_SOIL);
        }
    }
    CHECK_EQ_INT(map_ground_level(&map, 10, 3), 1);
    CHECK_EQ_INT(map_ground_level(&map, 3, 3), 0);
    CHECK(map_is_passable(&map, 10, 3, 1));
    CHECK(!map_is_open(&map, 10, 3, 0)); /* Earth. */
    CHECK(map_is_buildable(&map, 10, 3, 1));
    CHECK(!map_is_buildable(&map, 10, 3, 0));
    CHECK(!map_is_buildable(&map, 3, 3, 1));
    CHECK_EQ_INT(rock_def(ROCK_EARTH)->mined_item, ITEM_NONE);

    /* A slope at the foot of the hill leads up, orthogonally only. */
    TilePos links[MAP_MAX_LEVEL_LINKS];
    CHECK(!map_can_climb(&map, 7, 5, 0, 8, 5));
    CHECK_EQ_INT(map_level_links(&map, 7, 5, 0, links), 0);
    uint32_t revision = map.topology_revision;
    map_set_slope(&map, 7, 5, 0, true);
    CHECK(map.topology_revision != revision);
    CHECK_EQ_INT(map.slope_count, 1);
    CHECK(map_is_slope(&map, 7, 5, 0));
    CHECK(!map_is_buildable(&map, 7, 5, 0));
    CHECK(map_can_climb(&map, 7, 5, 0, 8, 5));
    CHECK(!map_can_climb(&map, 7, 5, 0, 8, 6)); /* Diagonal. */
    CHECK(!map_can_climb(&map, 7, 5, 0, 6, 5)); /* Open space up there. */
    CHECK_EQ_INT(map_level_links(&map, 7, 5, 0, links), 1);
    CHECK(links[0].x == 8 && links[0].y == 5 && links[0].z == 1);
    CHECK_EQ_INT(map_level_links(&map, 8, 5, 1, links), 1); /* And back down. */
    CHECK(links[0].x == 7 && links[0].y == 5 && links[0].z == 0);
    CHECK_EQ_INT(map_level_links(&map, 9, 5, 1, links), 0);

    /* A slope at a corner of the hill leads two ways. */
    for (int y = 10; y < 16; y++) {
        map_set_rock(&map, 8, y, 0, ROCK_NONE);
        map_set_terrain(&map, 8, y, 1, TERRAIN_OPEN);
    }
    map_set_slope(&map, 8, 10, 0, true);
    CHECK_EQ_INT(map_level_links(&map, 8, 10, 0, links), 2); /* East and north. */

    /* Something over the slope, or on it, blocks the way. */
    map_set_terrain(&map, 7, 5, 1, TERRAIN_SOIL);
    CHECK(!map_can_climb(&map, 7, 5, 0, 8, 5));
    CHECK(map_natural_roof(&map, 7, 5, 0));
    map_set_terrain(&map, 7, 5, 1, TERRAIN_OPEN);
    revision = map.topology_revision;
    map_set_rock(&map, 7, 5, 1, ROCK_GRANITE); /* Was open: a topology change. */
    CHECK(map.topology_revision != revision);
    CHECK(!map_can_climb(&map, 7, 5, 0, 8, 5));
    map_set_rock(&map, 7, 5, 1, ROCK_NONE);
    map_set_structure(&map, 7, 5, 0, STRUCTURE_WALL);
    CHECK(!map_can_climb(&map, 7, 5, 0, 8, 5));
    map_set_structure(&map, 7, 5, 0, STRUCTURE_NONE);
    CHECK(map_can_climb(&map, 7, 5, 0, 8, 5));

    /* Slopes are part of the hash. */
    uint64_t hash = map_hash(&map);
    map_set_slope(&map, 7, 5, 0, false);
    CHECK_EQ_INT(map.slope_count, 1);
    CHECK(map_hash(&map) != hash);
    map_free(&map);
}

static void test_levels(void)
{
    Map map;
    CHECK(!map_init(&map, 16, 16, 0));
    CHECK(!map_init(&map, 16, 16, MAP_MAX_LEVELS + 1));
    REQUIRE(map_init(&map, 20, 16, 3));
    CHECK_EQ_INT(map.depth, 3);
    CHECK_EQ_UINT(map_level_tile_count(&map), 320);
    CHECK_EQ_UINT(map_tile_count(&map), 960);
    CHECK(map_in_bounds(&map, 19, 15, 2));
    CHECK(!map_in_bounds(&map, 0, 0, 3));
    CHECK(!map_in_bounds(&map, 0, 0, -1));
    CHECK_EQ_UINT(map_index(&map, 5, 2, 0), 45);
    CHECK_EQ_UINT(map_index(&map, 5, 2, 2), 2 * 320 + 45);
    for (size_t i = 0; i < map_tile_count(&map); i += 37) {
        int x, y, z;
        map_tile_of(&map, i, &x, &y, &z);
        CHECK_EQ_UINT(map_index(&map, x, y, z), i);
    }
    CHECK_EQ_INT(map_ground_level(&map, 4, 4), 0);

    /* Levels do not touch: a wall on level 1 stays there. */
    map_set_structure(&map, 4, 4, 1, STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure(&map, 4, 4, 1), STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure(&map, 4, 4, 0), STRUCTURE_NONE);
    CHECK(!map_is_passable(&map, 4, 4, 1));
    CHECK(map_is_passable(&map, 4, 4, 0));
    CHECK(map_roof_supported(&map, 5, 4, 1));
    CHECK(!map_roof_supported(&map, 5, 4, 0));
    CHECK_EQ_INT(map.build_log[map.build_revision % MAP_BUILD_LOG_SIZE].z, 1);
    map_set_rock(&map, 8, 8, 2, ROCK_GRANITE);
    CHECK_EQ_INT(map.topology_log[map.topology_revision % MAP_TOPOLOGY_LOG_SIZE].z, 2);
    map_set_roof(&map, 5, 4, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK(map_is_roofed(&map, 5, 4, 1));
    CHECK(!map_is_roofed(&map, 5, 4, 0));
    map_set_stones(&map, 2, 2, 2, 5, 1);
    CHECK_EQ_INT(map_stones(&map, 2, 2, 2, ITEM_COBBLE), 5);
    CHECK_EQ_INT(map_stones(&map, 2, 2, 0, ITEM_COBBLE), 0);
    map_free(&map);

    /* The number of levels is part of the hash (a one-level map hashes as
     * before levels existed). */
    Map a, b;
    REQUIRE(map_init(&a, 16, 16, 1));
    REQUIRE(map_init(&b, 16, 16, 2));
    CHECK(map_hash(&a) != map_hash(&b));
    map_free(&a);
    map_free(&b);
}

static void test_passability_and_move_cost(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));

    CHECK(map_is_passable(&map, 1, 1, 0));
    CHECK_NEAR(map_move_cost(&map, 1, 1, 0), 1.0, 1e-6);
    CHECK(!map_is_passable(&map, -1, 0, 0));

    map.terrain[map_index(&map, 2, 2, 0)] = TERRAIN_DEEP_WATER;
    CHECK(!map_is_passable(&map, 2, 2, 0));

    map.rock[map_index(&map, 3, 3, 0)] = ROCK_GRANITE;
    CHECK(!map_is_passable(&map, 3, 3, 0));

    map.plants[map_index(&map, 4, 4, 0)].type = PLANT_TREE;
    CHECK_NEAR(map_move_cost(&map, 4, 4, 0), plant_def(PLANT_TREE)->move_cost, 1e-6);

    map.terrain[map_index(&map, 5, 5, 0)] = TERRAIN_SHALLOW_WATER;
    CHECK(map_move_cost(&map, 5, 5, 0) > 1.0f);
    CHECK_NEAR(map_move_cost_at(&map, map_index(&map, 4, 4, 0)), map_move_cost(&map, 4, 4, 0),
               1e-6);
    map_free(&map);
}

static void test_hash_detects_changes(void)
{
    Map a, b;
    REQUIRE(map_init(&a, 24, 24, 1));
    REQUIRE(map_init(&b, 24, 24, 1));
    CHECK_EQ_UINT(map_hash(&a), map_hash(&b));

    b.plants[10].type = PLANT_GRASS;
    CHECK(map_hash(&a) != map_hash(&b));
    a.plants[10].type = PLANT_GRASS;
    CHECK_EQ_UINT(map_hash(&a), map_hash(&b));

    b.plants[10].growth = 0.5f;
    CHECK(map_hash(&a) != map_hash(&b));
    map_free(&a);
    map_free(&b);

    REQUIRE(map_init(&a, 16, 32, 1));
    REQUIRE(map_init(&b, 32, 16, 1));
    CHECK(map_hash(&a) != map_hash(&b)); /* Same tile count, different shape. */
    map_free(&a);
    map_free(&b);
}

static void test_count_plants(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    map.plants[0].type = PLANT_TREE;
    map.plants[1].type = PLANT_TREE;
    map.plants[2].type = PLANT_BRAMBLE;
    CHECK_EQ_UINT(map_count_plants(&map, PLANT_TREE), 2);
    CHECK_EQ_UINT(map_count_plants(&map, PLANT_BRAMBLE), 1);
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
        /* Grows, or appears fully grown and goes (mushrooms). */
        CHECK(def->vanishes || (def->growth_per_day > 0.0f && def->growth_days != 0));
        CHECK(def->move_cost >= 1.0f);
        CHECK((def->growth_days & ~PLANT_ALL_YEAR) == 0);
        /* Either drops something when cut, or nothing at all. */
        CHECK((def->cut_item == ITEM_NONE) == (def->cut_yield == 0));
        for (int stock = 0; stock < PLANT_STOCK_COUNT; stock++) {
            /* A stock grows back, or there is none. */
            CHECK((def->stock_capacity[stock] > 0) == (def->stock_regrow_hours[stock] > 0));
        }
        /* Food: an edible item, a season, a stock. */
        bool food = def->food_item != ITEM_NONE;
        CHECK(!food || item_is_food(def->food_item));
        CHECK(food == (def->food_days != 0));
        CHECK(food == (def->stock_capacity[PLANT_STOCK_FOOD] > 0));
        CHECK((def->food_days & ~PLANT_ALL_YEAR) == 0);
        CHECK(food || (!def->food_dug && !def->food_confusable && !def->dug_whole));
        CHECK(!def->dug_whole || def->food_dug);
        CHECK(!def->vanishes || food);
    }
    CHECK(terrain_def(TERRAIN_COUNT) == NULL);
    CHECK(rock_def(ROCK_COUNT) == NULL);
    CHECK(plant_def(PLANT_COUNT) == NULL);
}

static void test_edible_plants(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    Plant *bramble = &map.plants[map_index(&map, 3, 3, 0)];
    *bramble = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    CHECK(!map_has_edible_plant(&map, 3, 3, 0)); /* No fruit yet. */
    plant_settle(bramble, SEASON_DAY(SEASON_SUMMER, 10));
    CHECK(map_has_edible_plant(&map, 3, 3, 0));
    CHECK(plant_has_food(bramble));
    plant_take_stock(bramble, PLANT_STOCK_FOOD, 1000); /* Picked clean. */
    CHECK(!map_has_edible_plant(&map, 3, 3, 0));

    /* Roots and what could be mistaken for a poisonous plant are not eaten
     * in place, even in season. */
    Plant *carrot = &map.plants[map_index(&map, 6, 6, 0)];
    *carrot = (Plant){.type = PLANT_WILD_CARROT, .growth = 1.0f};
    plant_settle(carrot, SEASON_DAY(SEASON_AUTUMN, 5));
    CHECK(plant_has_food(carrot));
    CHECK(!map_has_edible_plant(&map, 6, 6, 0));
    Plant *mushrooms = &map.plants[map_index(&map, 7, 6, 0)];
    *mushrooms = (Plant){.type = PLANT_MUSHROOMS, .growth = 1.0f};
    plant_settle(mushrooms, SEASON_DAY(SEASON_AUTUMN, 5));
    CHECK(plant_has_food(mushrooms));
    CHECK(!map_has_edible_plant(&map, 7, 6, 0));

    Plant *tree = &map.plants[map_index(&map, 4, 4, 0)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    CHECK(!map_has_edible_plant(&map, 4, 4, 0));
    CHECK(!map_has_edible_plant(&map, 5, 5, 0));
    CHECK(!map_has_edible_plant(&map, -1, 5, 0));
    map_free(&map);
}

static void test_water_and_drinking_spots(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    map.terrain[map_index(&map, 5, 5, 0)] = TERRAIN_SHALLOW_WATER;
    map.terrain[map_index(&map, 10, 10, 0)] = TERRAIN_DEEP_WATER;
    map.terrain[map_index(&map, 0, 0, 0)] = TERRAIN_DEEP_WATER;

    CHECK(map_is_water(&map, 5, 5, 0));
    CHECK(map_is_water(&map, 10, 10, 0));
    CHECK(!map_is_water(&map, 6, 5, 0));
    CHECK(!map_is_water(&map, -1, 5, 0));

    CHECK(map_is_drinking_spot(&map, 5, 5, 0)); /* Standing in shallow water. */
    CHECK(map_is_drinking_spot(&map, 6, 5, 0)); /* Shore tiles, 4-neighborhood. */
    CHECK(map_is_drinking_spot(&map, 5, 4, 0));
    CHECK(!map_is_drinking_spot(&map, 6, 6, 0)); /* Diagonal only. */
    CHECK(!map_is_drinking_spot(&map, 10, 10, 0)); /* Deep water is not walkable. */
    CHECK(map_is_drinking_spot(&map, 10, 11, 0));
    CHECK(map_is_drinking_spot(&map, 1, 0, 0)); /* Map edge does not break the lookup. */
    CHECK(!map_is_drinking_spot(&map, 3, 3, 0));

    map.rock[map_index(&map, 11, 10, 0)] = ROCK_GRANITE;
    CHECK(!map_is_drinking_spot(&map, 11, 10, 0)); /* Rock walls are not standable. */
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
    plant_settle(&plant, SEASON_DAY(SEASON_AUTUMN, 1));
    CHECK(!plant_has_food(&plant)); /* Trees bear no food. */

    const PlantDef *bramble = plant_def(PLANT_BRAMBLE);
    plant = (Plant){.type = PLANT_BRAMBLE, .growth = bramble->min_food_growth};
    plant_settle(&plant, SEASON_DAY(SEASON_SUMMER, 12));
    CHECK(plant_has_food(&plant));
    plant = (Plant){.type = PLANT_BRAMBLE, .growth = bramble->min_food_growth - 0.01f};
    plant_settle(&plant, SEASON_DAY(SEASON_SUMMER, 12));
    CHECK(!plant_has_food(&plant)); /* Too young to bear fruit. */
    plant = (Plant){.type = PLANT_COUNT, .growth = 1.0f};
    CHECK(!plant_has_food(&plant));
}

static void test_plant_days(void)
{
    PlantDays days = PLANT_DAYS(3, 5);
    CHECK(!plant_days_contain(days, 2));
    CHECK(plant_days_contain(days, 3));
    CHECK(plant_days_contain(days, 5));
    CHECK(!plant_days_contain(days, 6));
    CHECK(!plant_days_contain(days, -1));
    CHECK(plant_days_contain(PLANT_DAYS(0, 0), 0));
    CHECK(plant_days_contain(PLANT_ALL_YEAR, DAYS_PER_YEAR - 1));
    CHECK(!plant_days_contain(PLANT_ALL_YEAR, DAYS_PER_YEAR));
    CHECK(plant_days_contain(PLANT_DAYS(DAYS_PER_YEAR - 1, DAYS_PER_YEAR - 1), DAYS_PER_YEAR - 1));
}

/* Advances a plant on rich soil, day by day, from `first` to `last` (days of
 * the year, both included). */
static void advance_days(Plant *plant, int first, int last)
{
    float fertility = terrain_def(TERRAIN_RICH_SOIL)->fertility;
    for (int day = first; day <= last; day++) {
        for (int hour = 0; hour < HOURS_PER_DAY; hour++) {
            plant_advance(plant, fertility, 1, day);
        }
    }
}

/* Fruit ripens in its season, two handfuls a day, and is gone after it. */
static void test_fruit_follows_the_season(void)
{
    const PlantDef *def = plant_def(PLANT_BRAMBLE);
    int ripening = SEASON_DAY(SEASON_SUMMER, 8);
    Plant plant = {.type = PLANT_BRAMBLE, .growth = 1.0f};
    advance_days(&plant, SEASON_DAY(SEASON_SPRING, 1), ripening - 1);
    CHECK_EQ_INT(plant_stock_available(&plant, PLANT_STOCK_FOOD), 0); /* Flowers only. */
    advance_days(&plant, ripening, ripening);
    CHECK_EQ_INT(plant_stock_available(&plant, PLANT_STOCK_FOOD),
                 HOURS_PER_DAY / def->stock_regrow_hours[PLANT_STOCK_FOOD]);
    advance_days(&plant, ripening + 1, ripening + 10);
    CHECK_EQ_INT(plant_stock_available(&plant, PLANT_STOCK_FOOD),
                 def->stock_capacity[PLANT_STOCK_FOOD]);
    /* Picked, it ripens more while the season lasts. */
    CHECK_EQ_INT(plant_take_stock(&plant, PLANT_STOCK_FOOD, 1000),
                 def->stock_capacity[PLANT_STOCK_FOOD]);
    CHECK_NEAR(plant.growth, 1.0f, 1e-6); /* The bramble itself is untouched. */
    advance_days(&plant, ripening + 11, ripening + 11);
    CHECK(plant_has_food(&plant));
    /* Autumn goes on: the last blackberries fall and rot. */
    advance_days(&plant, ripening + 12, SEASON_DAY(SEASON_AUTUMN, 8));
    CHECK(!plant_has_food(&plant));
    CHECK_NEAR(plant.growth, 1.0f, 1e-6);
}

/* Wild garlic withers after spring and comes back the next one; nettles rest
 * in winter only; plants grow from spring to autumn, not in winter. */
static void test_plants_rest_out_of_season(void)
{
    Plant garlic = {.type = PLANT_WILD_GARLIC, .growth = 1.0f};
    plant_settle(&garlic, SEASON_DAY(SEASON_SPRING, 5));
    CHECK(plant_has_food(&garlic));
    CHECK(!plant_is_dormant(&garlic));
    advance_days(&garlic, SEASON_DAY(SEASON_SPRING, 5), SEASON_DAY(SEASON_SUMMER, 5));
    CHECK(plant_is_dormant(&garlic));
    CHECK_EQ_INT(garlic.type, PLANT_WILD_GARLIC); /* The bulb waits underground. */
    CHECK(!plant_has_food(&garlic));
    advance_days(&garlic, SEASON_DAY(SEASON_SUMMER, 6), DAYS_PER_YEAR - 1);
    CHECK(plant_is_dormant(&garlic));
    advance_days(&garlic, SEASON_DAY(SEASON_SPRING, 1), SEASON_DAY(SEASON_SPRING, 6));
    CHECK(!plant_is_dormant(&garlic));
    CHECK(plant_has_food(&garlic));

    /* Settled in the middle of summer (world generation): withered already. */
    Plant settled = {.type = PLANT_WILD_GARLIC, .growth = 0.8f};
    plant_settle(&settled, GAME_START_DAY);
    CHECK(plant_is_dormant(&settled));
    Plant nettle = {.type = PLANT_NETTLE, .growth = 0.8f};
    plant_settle(&nettle, GAME_START_DAY);
    CHECK(!plant_is_dormant(&nettle));
    CHECK(!plant_has_food(&nettle)); /* Tough leaves in summer. */
    plant_settle(&nettle, SEASON_DAY(SEASON_WINTER, 2));
    CHECK(plant_is_dormant(&nettle));

    /* No growth in winter, nor on too poor a ground. */
    Plant tree = {.type = PLANT_TREE, .growth = 0.5f};
    advance_days(&tree, SEASON_DAY(SEASON_WINTER, 1), SEASON_DAY(SEASON_WINTER, 10));
    CHECK_NEAR(tree.growth, 0.5f, 1e-6);
    for (int hour = 0; hour < HOURS_PER_DAY; hour++) {
        plant_advance(&tree, 0.1f, 1, SEASON_DAY(SEASON_SPRING, 1));
    }
    CHECK_NEAR(tree.growth, 0.5f, 1e-6);
    advance_days(&tree, SEASON_DAY(SEASON_SPRING, 1), SEASON_DAY(SEASON_SPRING, 1));
    CHECK_NEAR(tree.growth,
               0.5f + plant_def(PLANT_TREE)->growth_per_day *
                          terrain_def(TERRAIN_RICH_SOIL)->fertility,
               0.001);

    /* Cattails grow in the water, where nothing else does. */
    Plant cattail = {.type = PLANT_CATTAIL, .growth = 0.5f};
    for (int hour = 0; hour < HOURS_PER_DAY; hour++) {
        plant_advance(&cattail, 0.0f, 1, SEASON_DAY(SEASON_SPRING, 1));
    }
    CHECK_NEAR(cattail.growth, 0.5f + plant_def(PLANT_CATTAIL)->growth_per_day, 0.001);
}

/* Mushrooms go once their food is gone: picked, or out of season. */
static void test_mushrooms_vanish(void)
{
    Plant patch = {.type = PLANT_MUSHROOMS, .growth = 1.0f};
    plant_settle(&patch, SEASON_DAY(SEASON_AUTUMN, 3));
    REQUIRE(plant_has_food(&patch));
    advance_days(&patch, SEASON_DAY(SEASON_AUTUMN, 3), SEASON_DAY(SEASON_AUTUMN, 15));
    CHECK_EQ_INT(patch.type, PLANT_MUSHROOMS);
    plant_advance(&patch, 1.0f, 1, SEASON_DAY(SEASON_WINTER, 1));
    CHECK_EQ_INT(patch.type, PLANT_NONE); /* The first frost. */
}

static void test_tree_near(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    map.plants[map_index(&map, 5, 5, 0)] = (Plant){.type = PLANT_TREE, .growth = 0.9f};
    CHECK(!map_tree_near(&map, 6, 6, 0, 2)); /* Not mature yet. */
    map.plants[map_index(&map, 5, 5, 0)].growth = 1.0f;
    CHECK(map_tree_near(&map, 6, 6, 0, 2));
    CHECK(map_tree_near(&map, 7, 3, 0, 2));
    CHECK(!map_tree_near(&map, 8, 5, 0, 2));
    CHECK(map_tree_near(&map, 5, 5, 0, 0));
    map_free(&map);
}

/* 16 x 16, 3 levels: plains on level 0; a hill of earth for x >= 4 (earth
 * on level 0, its ground on level 1); a 5 x 5 room dug into it (x and y 5 to
 * 9), its middle (7, 7) out of reach of the rock around it. */
static bool init_dug_room(Map *map)
{
    if (!map_init(map, 16, 16, 3)) {
        return false;
    }
    for (int y = 0; y < 16; y++) {
        for (int x = 4; x < 16; x++) {
            bool room = x >= 5 && x <= 9 && y >= 5 && y <= 9;
            map->rock[map_index(map, x, y, 0)] = room ? ROCK_NONE : ROCK_EARTH;
            map->terrain[map_index(map, x, y, 1)] = TERRAIN_SOIL;
        }
    }
    map_topology_changed(map); /* The layers were written directly. */
    return true;
}

/* Ceilings: the ground over a dug tile, held up by rock, a wall or a prop
 * within ROOF_SUPPORT_RANGE tiles. */
/* A ladder leads up where a slope would, slower; it needs somewhere to lead
 * to. */
static void test_ladders(void)
{
    Map map;
    REQUIRE(init_dug_room(&map));
    CHECK(map_ladder_fits(&map, 3, 2, 0)); /* At the foot of the hill. */
    CHECK(!map_ladder_fits(&map, 1, 2, 0)); /* Plains all around. */
    CHECK(!map_ladder_fits(&map, 6, 6, 0)); /* In the room: ground over it. */
    CHECK(!map_ladder_fits(&map, 3, 2, 2)); /* The top level. */
    CHECK_NEAR(map_climb_cost(&map, 3, 2, 0), 0.0, 1e-6);
    CHECK(!map_can_climb(&map, 3, 2, 0, 4, 2));
    map_set_structure(&map, 3, 2, 0, STRUCTURE_LADDER);
    CHECK(map_is_passable(&map, 3, 2, 0)); /* Walked past. */
    CHECK_NEAR(map_climb_cost(&map, 3, 2, 0), MAP_LADDER_CLIMB_COST, 1e-6);
    CHECK(map_can_climb(&map, 3, 2, 0, 4, 2));
    CHECK(!map_can_climb(&map, 3, 2, 0, 2, 2)); /* No ground up there. */
    TilePos links[MAP_MAX_LEVEL_LINKS];
    CHECK_EQ_INT(map_level_links(&map, 4, 2, 1, links), 1); /* Down the ladder. */
    CHECK(links[0].x == 3 && links[0].y == 2 && links[0].z == 0);
    map_set_slope(&map, 3, 3, 0, true);
    CHECK_NEAR(map_climb_cost(&map, 3, 3, 0), MAP_CLIMB_COST, 1e-6);
    CHECK_NEAR(map_climb_cost(&map, -1, 3, 0), 0.0, 1e-6);
    map_free(&map);
}

static void test_ceilings_need_support(void)
{
    Map map;
    REQUIRE(init_dug_room(&map));
    CHECK(map_has_ceiling(&map, 5, 5, 0));
    CHECK(!map_has_ceiling(&map, 2, 5, 0)); /* Open sky. */
    CHECK(!map_has_ceiling(&map, 4, 5, 0)); /* Rock. */
    CHECK(!map_has_ceiling(&map, 5, 5, 2)); /* The top level. */
    CHECK(map_ceiling_supported(&map, 5, 5, 0));
    CHECK(map_ceiling_supported(&map, 2, 5, 0)); /* Nothing to hold up. */
    CHECK(!map_ceiling_supported(&map, 7, 7, 0)); /* 3 tiles from the rock. */
    CHECK(map_ceiling_supported(&map, 7, 5, 0));

    /* A prop holds up what is within reach, its own tile included. */
    map_set_structure(&map, 7, 7, 0, STRUCTURE_PROP);
    CHECK(map_ceiling_supported(&map, 7, 7, 0));
    CHECK(map_dig_keeps_ceilings(&map, 10, 7, 0, false)); /* Widening the room. */
    map_set_structure(&map, 7, 7, 0, STRUCTURE_NONE);

    /* Digging: the rock dug out holds nothing up any more. */
    map_set_rock(&map, 7, 7, 0, ROCK_EARTH); /* A pillar left in the middle. */
    CHECK(map_dig_keeps_ceilings(&map, 4, 2, 0, false)); /* Away from the room. */
    CHECK(!map_dig_keeps_ceilings(&map, 7, 7, 0, false)); /* The pillar. */
    /* The room's wall: other rock of the wall still holds the room up. */
    CHECK(map_dig_keeps_ceilings(&map, 7, 10, 0, false));
    map_set_rock(&map, 7, 7, 0, ROCK_NONE);
    map_set_structure(&map, 7, 7, 0, STRUCTURE_PROP);
    /* A tile of rock deep inside the hill, far from the room: its own
     * ceiling is held by the rock around it. */
    CHECK(map_dig_keeps_ceilings(&map, 13, 13, 0, false));
    /* A ramp leaves its tile no ceiling: only its neighbors count. */
    CHECK(map_dig_keeps_ceilings(&map, 4, 12, 0, true));
    map_free(&map);
}

static void test_change_logs(void)
{
    Map map;
    REQUIRE(map_init(&map, 20, 16, 1));
    /* Structures and roofs: the build log. */
    uint32_t build = map.build_revision;
    map_set_structure(&map, 3, 4, 0, STRUCTURE_WALL);
    CHECK_EQ_UINT(map.build_revision, build + 1);
    const MapBuildChange *change = &map.build_log[map.build_revision % MAP_BUILD_LOG_SIZE];
    CHECK_EQ_INT(change->x, 3);
    CHECK_EQ_INT(change->y, 4);
    map_set_roof(&map, 7, 8, 0, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    change = &map.build_log[map.build_revision % MAP_BUILD_LOG_SIZE];
    CHECK_EQ_INT(change->x, 7);
    CHECK_EQ_INT(change->y, 8);
    /* Rock on a wall: nobody could walk there anyway, but it is a change. */
    uint32_t topology = map.topology_revision;
    build = map.build_revision;
    map_set_rock(&map, 3, 4, 0, ROCK_GRANITE);
    CHECK_EQ_UINT(map.topology_revision, topology);
    CHECK_EQ_UINT(map.build_revision, build + 1);
    /* Rock on open ground: the topology log, on its tile. */
    map_set_rock(&map, 10, 10, 0, ROCK_GRANITE);
    const MapTopologyChange *moved = &map.topology_log[map.topology_revision %
                                                       MAP_TOPOLOGY_LOG_SIZE];
    CHECK_EQ_INT(moved->x, 10);
    CHECK_EQ_INT(moved->y, 10);
    CHECK(!moved->opened && !moved->anywhere);
    /* Layers written directly: anywhere. */
    map_topology_changed(&map);
    moved = &map.topology_log[map.topology_revision % MAP_TOPOLOGY_LOG_SIZE];
    CHECK(moved->anywhere);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_init_rejects_invalid_sizes);
    RUN_TEST(test_init_fills_with_soil);
    RUN_TEST(test_bounds_and_index);
    RUN_TEST(test_levels);
    RUN_TEST(test_floors_and_slopes);
    RUN_TEST(test_passability_and_move_cost);
    RUN_TEST(test_hash_detects_changes);
    RUN_TEST(test_count_plants);
    RUN_TEST(test_definitions_are_complete);
    RUN_TEST(test_edible_plants);
    RUN_TEST(test_water_and_drinking_spots);
    RUN_TEST(test_plant_growth_stages_and_fruit);
    RUN_TEST(test_plant_days);
    RUN_TEST(test_fruit_follows_the_season);
    RUN_TEST(test_plants_rest_out_of_season);
    RUN_TEST(test_mushrooms_vanish);
    RUN_TEST(test_tree_near);
    RUN_TEST(test_ladders);
    RUN_TEST(test_ceilings_need_support);
    RUN_TEST(test_change_logs);
    return test_summary();
}
