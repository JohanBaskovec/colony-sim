#include "core/i18n.h"
#include "core/item.h"
#include "core/map.h"
#include "core/pathfinding.h"
#include "core/temperature.h"

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
    CHECK(!map_is_open(&map, 3, 3, 3));     /* Out of bounds. */
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
    /* Shown as a ramp up on its level, down from the level above. */
    CHECK_EQ_INT(map_ramp_at(&map, 7, 5, 0), MAP_RAMP_UP);
    CHECK_EQ_INT(map_ramp_at(&map, 7, 5, 1), MAP_RAMP_DOWN);
    CHECK_EQ_INT(map_ramp_at(&map, 7, 5, 2), MAP_RAMP_NONE);
    CHECK_EQ_INT(map_ramp_at(&map, 8, 5, 1), MAP_RAMP_NONE); /* Its top: ground. */
    CHECK_EQ_INT(map_ramp_at(&map, 6, 5, 0), MAP_RAMP_NONE);
    CHECK_EQ_INT(map_ramp_at(&map, 6, 5, 1), MAP_RAMP_NONE); /* Open, no slope below. */
    CHECK_EQ_INT(map_ramp_at(&map, 7, 5, -1), MAP_RAMP_NONE);
    CHECK_EQ_INT(map_ramp_at(&map, -1, 5, 0), MAP_RAMP_NONE);

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
    CHECK_EQ_INT(map_ramp_at(&map, 7, 5, 0), MAP_RAMP_NONE); /* Leads nowhere. */
    CHECK_EQ_INT(map_ramp_at(&map, 7, 5, 1), MAP_RAMP_NONE);
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
    CHECK(map_roof_supported(&map, 5, 4, 2)); /* A roof over level 1. */
    CHECK(!map_roof_supported(&map, 5, 4, 1));
    CHECK_EQ_INT(map.build_log[map.build_revision % MAP_BUILD_LOG_SIZE].z, 1);
    map_set_rock(&map, 8, 8, 2, ROCK_GRANITE);
    CHECK_EQ_INT(map.topology_log[map.topology_revision % MAP_TOPOLOGY_LOG_SIZE].z, 2);
    map_set_roof(&map, 5, 4, 2, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK(map_is_roofed(&map, 5, 4, 1));
    CHECK(!map_is_roofed(&map, 5, 4, 2));
    CHECK(map_is_roofed(&map, 5, 4, 0)); /* Under the floor of level 1 anyway. */
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
        /* Trunks are felled, not mined (core/tree.h). */
        CHECK(r == ROCK_WOOD || rock_def((RockType)r)->mine_work > 0.0f);
        CHECK(item_def(rock_def((RockType)r)->mined_item) != NULL);
    }
    for (int p = PLANT_NONE + 1; p < PLANT_COUNT; p++) {
        const PlantDef *def = plant_def((PlantType)p);
        CHECK(i18n_is_valid_key(def->name_key));
        /* Grows, or appears fully grown and goes (mushrooms). */
        CHECK(def->harvest == PLANT_HARVEST_REMOVES || def->growth_per_day > 0.0f);
        /* Temperatures in order: it grows, best, then not; frost and heat beyond. */
        CHECK(def->t_min < def->t_opt_low && def->t_opt_low <= def->t_opt_high &&
              def->t_opt_high < def->t_max);
        CHECK(def->t_frost_hardened <= def->t_frost_active && def->t_frost_active < def->t_min);
        CHECK(def->t_lethal_heat > def->t_max);
        CHECK(!def->rests_in_heat || def->rest == PLANT_REST_DIES_BACK);
        CHECK(def->move_cost >= 1.0f);
        /* Either drops something when cut, or nothing at all. */
        CHECK((def->cut_item == ITEM_NONE) == (def->cut_yield == 0));
        /* A product: an item, a stock that grows back, a season; a slot
         * without an item is empty. */
        int products = 0, foods = 0;
        for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
            const PlantProduct *product = &def->products[slot];
            bool used = product->item != ITEM_NONE;
            CHECK(used == (product->capacity > 0));
            CHECK(used == (product->regrow_hours > 0));
            /* A window of warmth that opens before it shuts, a chill that loses it. */
            CHECK(product->warmth_from >= 0.0f);
            CHECK(product->warmth_until == 0.0f || product->warmth_until > product->warmth_from);
            CHECK(product->lost_below >= 0.0f);
            CHECK(used || (!product->dug && !product->confusable && product->min_growth == 0.0f &&
                           product->warmth_from == 0.0f && product->lost_below == 0.0f));
            CHECK(!product->dug || item_is_food(product->item)); /* Roots to eat. */
            CHECK(plant_product((PlantType)p, slot) == (used ? product : NULL));
            products += used;
            foods += used && item_is_food(product->item);
        }
        CHECK(foods <= 1); /* One food: plant_food_slot(). */
        CHECK((plant_food_slot((PlantType)p) >= 0) == (foods == 1));
        CHECK(!plant_is_tree((uint8_t)p) || products == 0); /* Trees shed, they do not bear. */
        /* A plant that is taken whole is gone, or grows again from nothing. */
        CHECK(def->harvest == PLANT_HARVEST_KEEPS || products > 0);
        CHECK(!def->grazed || def->growth_per_day > 0.0f);
    }
    CHECK(terrain_def(TERRAIN_COUNT) == NULL);
    CHECK(rock_def(ROCK_COUNT) == NULL);
    CHECK(plant_def(PLANT_COUNT) == NULL);
}

/* Harvesting: what it does to the plant, by the data (PlantDef.harvest). */
static void test_harvest_effects(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    int autumn = SEASON_DAY(SEASON_AUTUMN, 5);
    Plant *bramble = &map.plants[map_index(&map, 1, 1, 0)];
    Plant *carrot = &map.plants[map_index(&map, 2, 1, 0)];
    Plant *mushrooms = &map.plants[map_index(&map, 3, 1, 0)];
    Plant *grass = &map.plants[map_index(&map, 4, 1, 0)];
    *bramble = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    *carrot = (Plant){.type = PLANT_WILD_CARROT, .growth = 1.0f};
    *mushrooms = (Plant){.type = PLANT_MUSHROOMS, .growth = 1.0f};
    *grass = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    plant_settle(bramble, SEASON_DAY(SEASON_SUMMER, 10));
    plant_settle(carrot, autumn);
    plant_settle(mushrooms, autumn);
    plant_settle(grass, autumn);
    for (int x = 1; x <= 4; x++) {
        Plant *plant = &map.plants[map_index(&map, x, 1, 0)];
        REQUIRE(plant_stock_available(plant, 0) > 0);
        plant_take_stock(plant, 0, 1000);
        plant_harvested(&map, x, 1, 0);
    }
    CHECK_EQ_INT(bramble->type, PLANT_BRAMBLE); /* Keeps: the stock grows back. */
    CHECK_NEAR(bramble->growth, 1.0f, 1e-6);
    CHECK_EQ_INT(carrot->type, PLANT_WILD_CARROT); /* Resets: grows again. */
    CHECK_NEAR(carrot->growth, 0.0f, 1e-6);
    CHECK_EQ_INT(grass->type, PLANT_GRASS);
    CHECK_NEAR(grass->growth, 0.0f, 1e-6);
    CHECK_EQ_INT(mushrooms->type, PLANT_NONE); /* Removes: the tile is bare. */
    plant_harvested(&map, -1, 1, 0);           /* Outside the map: nothing. */
    map_free(&map);
}

/* Grass grows like any plant and its stock of armfuls with it, whatever the
 * season (what grows back is capped by its growth). */
static void test_grass_stock_follows_growth(void)
{
    Plant grass = {.type = PLANT_GRASS, .growth = 0.0f};
    const PlantProduct *armfuls = plant_product(PLANT_GRASS, 0);
    REQUIRE(armfuls != NULL);
    CHECK_EQ_INT(plant_stock_capacity(&grass, 0), 0);
    PlantConditions warm = {
        .temperature = 20.0f, .fertility = 1.0f, .light = 1.0f, .light_peak = 1.0f};
    int previous = 0;
    for (int hours = 0; hours < 10 * HOURS_PER_DAY && grass.growth < 1.0f; hours += 6) {
        plant_advance(&grass, &warm, 6);
        int available = plant_stock_available(&grass, 0);
        CHECK(available >= previous); /* Never less as it grows. */
        CHECK(available <= plant_stock_capacity(&grass, 0));
        previous = available;
    }
    CHECK_NEAR(grass.growth, 1.0f, 1e-6);
    plant_advance(&grass, &warm, HOURS_PER_DAY);
    CHECK_EQ_INT(plant_stock_available(&grass, 0), armfuls->capacity);
    /* Eaten down by a grazer: the stock follows the growth. */
    grass.growth = 0.2f;
    plant_advance(&grass, &warm, 1);
    CHECK_EQ_INT(plant_stock_available(&grass, 0), 0);
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
    plant_take_stock(bramble, 0, 1000); /* Picked clean. */
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
    CHECK(!map_is_drinking_spot(&map, 6, 6, 0));   /* Diagonal only. */
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
    plant = (Plant){.type = PLANT_BRAMBLE, .growth = bramble->products[0].min_growth};
    plant_settle(&plant, SEASON_DAY(SEASON_SUMMER, 12));
    CHECK(plant_has_food(&plant));
    plant = (Plant){.type = PLANT_BRAMBLE, .growth = bramble->products[0].min_growth - 0.01f};
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

/* Advances a plant on rich soil in the open, hour by hour in the outdoor
 * temperatures of the days from `first` to `last` (days of the year, both
 * included). */
static void advance_days(Plant *plant, int first, int last)
{
    float fertility = terrain_def(TERRAIN_RICH_SOIL)->fertility;
    for (int day = first; day <= last; day++) {
        for (int hour = 0; hour < HOURS_PER_DAY; hour++) {
            uint64_t tick = (uint64_t)day * TICKS_PER_DAY + (uint64_t)hour * TICKS_PER_HOUR;
            PlantConditions conditions = {
                .temperature = temperature_outdoor(tick),
                .fertility = fertility,
                .light = game_sunlight(tick), /* In the open, under a clear sky. */
                .light_peak = game_sun_noon(day),
            };
            plant_advance(plant, &conditions, 1);
        }
    }
}

/* Fruit ripens when the plant has gathered enough warmth (the end of summer in
 * this climate), a bit at a time, and is spoiled by the autumn chill. */
static void test_fruit_follows_the_season(void)
{
    const PlantDef *def = plant_def(PLANT_BRAMBLE);
    Plant plant = {.type = PLANT_BRAMBLE, .growth = 1.0f};
    int day = SEASON_DAY(SEASON_SPRING, 1);
    while (day < DAYS_PER_YEAR && !plant_has_food(&plant)) {
        advance_days(&plant, day, day);
        CHECK_EQ_INT(plant_stock_available(&plant, 0), 0); /* Flowers only, so far. */
        day++;
        advance_days(&plant, day, day);
        if (plant_has_food(&plant)) {
            break;
        }
    }
    CHECK(day >= SEASON_DAY(SEASON_SUMMER, 1) && day <= SEASON_DAY(SEASON_SUMMER, 15));
    CHECK(plant_stock_available(&plant, 0) < def->products[0].capacity); /* A few at first. */
    advance_days(&plant, day + 1, day + 10);
    CHECK_EQ_INT(plant_stock_available(&plant, 0), def->products[0].capacity);
    /* Picked, it ripens more while the season lasts. */
    CHECK_EQ_INT(plant_take_stock(&plant, 0, 1000), def->products[0].capacity);
    CHECK_NEAR(plant.growth, 1.0f, 1e-6); /* The bramble itself is untouched. */
    advance_days(&plant, day + 11, day + 11);
    CHECK(plant_has_food(&plant));
    /* Autumn goes on: the chill spoils the last blackberries. */
    advance_days(&plant, day + 12, SEASON_DAY(SEASON_WINTER, 1));
    CHECK(!plant_has_food(&plant));
    CHECK_NEAR(plant.growth, 1.0f, 1e-6);
}

/* Wild garlic dies back in the warmth and comes back the next spring; nettles
 * die back in the cold; plants grow when it is warm enough, not in winter. */
static void test_plants_rest_by_the_climate(void)
{
    Plant garlic = {.type = PLANT_WILD_GARLIC, .growth = 1.0f};
    plant_settle(&garlic, SEASON_DAY(SEASON_SPRING, 5));
    CHECK(plant_has_food(&garlic));
    CHECK(!plant_is_dormant(&garlic));
    advance_days(&garlic, SEASON_DAY(SEASON_SPRING, 5), SEASON_DAY(SEASON_SUMMER, 10));
    CHECK(plant_is_dormant(&garlic));
    CHECK_EQ_INT(garlic.type, PLANT_WILD_GARLIC); /* The bulb waits underground. */
    CHECK(!plant_has_food(&garlic));
    advance_days(&garlic, SEASON_DAY(SEASON_SUMMER, 11), DAYS_PER_YEAR - 1);
    CHECK(plant_is_dormant(&garlic)); /* Dead above ground in the winter cold. */
    advance_days(&garlic, SEASON_DAY(SEASON_SPRING, 1), SEASON_DAY(SEASON_SPRING, 8));
    CHECK(!plant_is_dormant(&garlic));
    CHECK(plant_has_food(&garlic));

    /* Settled in the middle of summer (world generation): died back already. */
    Plant settled = {.type = PLANT_WILD_GARLIC, .growth = 0.8f};
    plant_settle(&settled, GAME_START_DAY);
    CHECK(plant_is_dormant(&settled));
    Plant nettle = {.type = PLANT_NETTLE, .growth = 0.8f};
    plant_settle(&nettle, GAME_START_DAY);
    CHECK(!plant_is_dormant(&nettle));
    CHECK(!plant_has_food(&nettle)); /* Tough leaves in summer. */
    plant_settle(&nettle, SEASON_DAY(SEASON_WINTER, 2));
    CHECK(plant_is_dormant(&nettle));
}

/* Growth follows the temperature: nothing in the cold, the best rate between
 * the plant's optimal temperatures, nothing on too poor a ground; a cattail
 * grows in the water, where the fertility counts as 1. */
static void test_growth_follows_the_temperature(void)
{
    const PlantDef *def = plant_def(PLANT_TREE);
    float rich = terrain_def(TERRAIN_RICH_SOIL)->fertility;
    int day = SEASON_DAY(SEASON_SUMMER, 5);
    PlantConditions best = {
        .temperature = def->t_opt_low, .fertility = rich, .light = 1.0f, .light_peak = 1.0f};
    Plant tree = {.type = PLANT_TREE, .growth = 0.5f};
    plant_settle(&tree, day);
    for (int hour = 0; hour < (int)PLANT_BEST_DAY_HOURS; hour++) {
        plant_advance(&tree, &best, 1);
    }
    CHECK_NEAR(tree.growth, 0.5f + def->growth_per_day * rich, 0.001);
    CHECK_NEAR(plant_growth_rate(&tree, &best), def->growth_per_day * rich, 1e-6);

    /* Half way up the slope to its best temperature: half the rate. */
    PlantConditions cool = best;
    cool.temperature = 0.5f * (def->t_min + def->t_opt_low);
    CHECK_NEAR(plant_temperature_response(PLANT_TREE, cool.temperature), 0.5, 1e-6);
    CHECK_NEAR(plant_growth_rate(&tree, &cool), 0.5 * def->growth_per_day * rich, 1e-6);
    CHECK_EQ_INT(plant_status(&tree, &cool), PLANT_STATUS_SLOWED_COLD);
    PlantConditions hot = best;
    hot.temperature = 0.5f * (def->t_opt_high + def->t_max);
    CHECK_NEAR(plant_temperature_response(PLANT_TREE, hot.temperature), 0.5, 1e-6);
    CHECK_EQ_INT(plant_status(&tree, &hot), PLANT_STATUS_SLOWED_HOT);
    CHECK_EQ_INT(plant_status(&tree, &best), PLANT_STATUS_GROWING);

    /* None below the minimum nor above the maximum, and on a poor ground. */
    float before = tree.growth;
    PlantConditions cold = best;
    cold.temperature = def->t_min - 1.0f;
    plant_advance(&tree, &cold, 1);
    CHECK_NEAR(tree.growth, before, 1e-9);
    CHECK_EQ_INT(plant_status(&tree, &cold), PLANT_STATUS_TOO_COLD);
    PlantConditions scorching = best;
    scorching.temperature = def->t_max + 1.0f;
    plant_advance(&tree, &scorching, 1);
    CHECK_NEAR(tree.growth, before, 1e-9);
    CHECK_EQ_INT(plant_status(&tree, &scorching), PLANT_STATUS_TOO_HOT);
    PlantConditions poor = best;
    poor.fertility = 0.1f;
    plant_advance(&tree, &poor, HOURS_PER_DAY);
    CHECK_NEAR(tree.growth, before, 1e-9);

    Plant cattail = {.type = PLANT_CATTAIL, .growth = 0.5f};
    plant_settle(&cattail, day);
    PlantConditions water = {.temperature = plant_def(PLANT_CATTAIL)->t_opt_low,
                             .fertility = 0.0f,
                             .light = 1.0f,
                             .light_peak = 1.0f};
    for (int hour = 0; hour < (int)PLANT_BEST_DAY_HOURS; hour++) { /* A best day. */
        plant_advance(&cattail, &water, 1);
    }
    CHECK_NEAR(cattail.growth, 0.5f + plant_def(PLANT_CATTAIL)->growth_per_day, 0.001);
}

/* Winter: a plant that rests grows nothing and keeps what it is; a plant that
 * dies back is gone above ground, whatever it was. */
static void test_nothing_grows_in_winter(void)
{
    Plant tree = {.type = PLANT_TREE, .growth = 0.5f};
    advance_days(&tree, SEASON_DAY(SEASON_WINTER, 1), SEASON_DAY(SEASON_WINTER, 10));
    CHECK_NEAR(tree.growth, 0.5f, 1e-6);
    CHECK_EQ_INT(tree.type, PLANT_TREE);
    Plant nettle = {.type = PLANT_NETTLE, .growth = 0.8f};
    advance_days(&nettle, SEASON_DAY(SEASON_AUTUMN, 5), SEASON_DAY(SEASON_WINTER, 10));
    CHECK(plant_is_dormant(&nettle));
    CHECK_NEAR(nettle.growth, 0.0f, 1e-6);
}

/* Conditions at a temperature, on rich soil, in summer (what stocks ask of the day). */
static PlantConditions at_temperature(float celsius)
{
    return (PlantConditions){.temperature = celsius,
                             .fertility = terrain_def(TERRAIN_RICH_SOIL)->fertility,
                             .light = 1.0f,
                             .light_peak = 1.0f};
}

/* A plant goes to rest after days of cold, not on a cold night, and wakes
 * after days of warmth, not on a warm day. */
static void test_cold_puts_plants_to_rest(void)
{
    Plant grass = {.type = PLANT_GRASS, .growth = 0.6f};
    plant_settle(&grass, SEASON_DAY(SEASON_SUMMER, 5));
    PlantConditions cold = at_temperature(0.0f);
    plant_advance(&grass, &cold, 24);
    CHECK_EQ_INT(plant_status(&grass, &cold), PLANT_STATUS_TOO_COLD); /* A day: still awake. */
    plant_advance(&grass, &cold, 7 * HOURS_PER_DAY);
    CHECK_EQ_INT(plant_status(&grass, &cold), PLANT_STATUS_RESTING);
    float growth = grass.growth;
    plant_advance(&grass, &cold, HOURS_PER_DAY);
    CHECK_NEAR(grass.growth, growth, 1e-9);
    CHECK(!plant_is_dormant(&grass)); /* Grass rests as it is: it shows. */
    /* A mild day wakes nothing, a warm week does. */
    PlantConditions mild = at_temperature(15.0f);
    plant_advance(&grass, &mild, HOURS_PER_DAY);
    CHECK_EQ_INT(plant_status(&grass, &mild), PLANT_STATUS_RESTING);
    plant_advance(&grass, &mild, 6 * HOURS_PER_DAY);
    CHECK_EQ_INT(plant_status(&grass, &mild), PLANT_STATUS_GROWING);
    /* A plant made by hand takes the climate of the tile it finds itself on. */
    Plant young = {.type = PLANT_GRASS, .growth = 0.3f};
    plant_advance(&young, &cold, 1);
    CHECK_EQ_INT(plant_status(&young, &cold), PLANT_STATUS_RESTING);
    /* Settled in winter: resting and hardened. */
    Plant winter = {.type = PLANT_GRASS, .growth = 0.5f};
    plant_settle(&winter, SEASON_DAY(SEASON_WINTER, 8));
    CHECK_EQ_INT(plant_status(&winter, &cold), PLANT_STATUS_RESTING);
    CHECK_NEAR(plant_frost_limit(&winter), plant_def(PLANT_GRASS)->t_frost_hardened, 1e-3);
}

/* A plant dies back where nothing grows above ground and the roots wait: no
 * frost harms it then. As the cold comes on, it has died back before the
 * frosts are hard. */
static void test_dying_back_spares_the_roots(void)
{
    Plant nettle = {.type = PLANT_NETTLE, .growth = 0.9f};
    plant_settle(&nettle, SEASON_DAY(SEASON_SUMMER, 5));
    advance_days(&nettle, SEASON_DAY(SEASON_AUTUMN, 1), SEASON_DAY(SEASON_WINTER, 8));
    CHECK_EQ_INT(nettle.type, PLANT_NETTLE);
    CHECK(plant_is_dormant(&nettle));
    CHECK_NEAR(nettle.growth, 0.0f, 1e-9);
    PlantConditions freezing = at_temperature(-30.0f);
    plant_advance(&nettle, &freezing, 6 * HOURS_PER_DAY);
    CHECK_EQ_INT(nettle.type, PLANT_NETTLE);
    CHECK_NEAR(plant_health(&nettle), 1.0f, 1e-3);
    CHECK_EQ_INT(plant_status(&nettle, &freezing), PLANT_STATUS_DIED_BACK);
    /* A sudden freeze catches a tender plant still growing. */
    Plant caught = {.type = PLANT_NETTLE, .growth = 0.9f};
    plant_settle(&caught, SEASON_DAY(SEASON_SUMMER, 5));
    plant_advance(&caught, &freezing, 6 * HOURS_PER_DAY);
    CHECK_EQ_INT(caught.type, PLANT_NONE);
}

/* Frost kills a tender plant in hours, not a hardened one; heat too. */
static void test_frost_and_heat_kill(void)
{
    const PlantDef *def = plant_def(PLANT_GRASS);
    Plant tender = {.type = PLANT_GRASS, .growth = 0.6f};
    plant_settle(&tender, SEASON_DAY(SEASON_SUMMER, 5));
    PlantConditions frost = at_temperature(def->t_frost_active - 5.0f);
    CHECK_EQ_INT(plant_status(&tender, &frost), PLANT_STATUS_FROST);
    plant_advance(&tender, &frost, 5); /* 0.02 × 5 degrees × 5 hours: half its health. */
    CHECK_EQ_INT(tender.type, PLANT_GRASS);
    CHECK_NEAR(plant_health(&tender), 0.5f, 0.01);
    CHECK_NEAR(plant_growth_rate(&tender, &frost), 0.0f, 1e-9); /* Too cold to grow anyway. */
    plant_advance(&tender, &frost, 6);
    CHECK_EQ_INT(tender.type, PLANT_NONE); /* Dead: the tile is bare. */

    Plant hardened = {.type = PLANT_GRASS, .growth = 0.6f};
    plant_settle(&hardened, SEASON_DAY(SEASON_WINTER, 8));
    PlantConditions hard = at_temperature(def->t_frost_active - 5.0f);
    plant_advance(&hardened, &hard, 2 * HOURS_PER_DAY);
    CHECK_EQ_INT(hardened.type, PLANT_GRASS);
    CHECK_NEAR(plant_health(&hardened), 1.0f, 1e-6);

    Plant hot = {.type = PLANT_GRASS, .growth = 0.6f};
    plant_settle(&hot, SEASON_DAY(SEASON_SUMMER, 5));
    PlantConditions furnace = at_temperature(def->t_lethal_heat + 5.0f);
    CHECK_EQ_INT(plant_status(&hot, &furnace), PLANT_STATUS_HEAT);
    plant_advance(&hot, &furnace, 12);
    CHECK_EQ_INT(hot.type, PLANT_NONE);
}

/* The cold hardens a plant over days and the warmth softens it again: a frost
 * that a winter plant takes in stride kills a spring one. */
static void test_hardening_takes_days(void)
{
    const PlantDef *def = plant_def(PLANT_BRAMBLE);
    Plant plant = {.type = PLANT_BRAMBLE, .growth = 0.8f};
    plant_settle(&plant, SEASON_DAY(SEASON_SUMMER, 5));
    CHECK_NEAR(plant_frost_limit(&plant), def->t_frost_active, 1e-6);
    PlantConditions chilly = at_temperature(def->t_min - 3.0f); /* Cold, but no frost. */
    plant_advance(&plant, &chilly, 24);
    float after_a_day = plant_frost_limit(&plant);
    CHECK(after_a_day < def->t_frost_active + 1e-6f);
    plant_advance(&plant, &chilly, 10 * HOURS_PER_DAY);
    CHECK_NEAR(plant_frost_limit(&plant), def->t_frost_hardened, 1e-3);
    PlantConditions warm = at_temperature(def->t_opt_low);
    plant_advance(&plant, &warm, 10 * HOURS_PER_DAY);
    CHECK_NEAR(plant_frost_limit(&plant), def->t_frost_active, 1e-3);
}

/* Damage mends in good weather (slowly), but not while the plant rests. */
static void test_plants_mend(void)
{
    Plant plant = {.type = PLANT_PLANTAIN, .growth = 0.7f};
    plant_settle(&plant, SEASON_DAY(SEASON_SUMMER, 5));
    PlantConditions frost = at_temperature(plant_def(PLANT_PLANTAIN)->t_frost_active - 2.0f);
    plant_advance(&plant, &frost, 3); /* 0.02 × 2 degrees × 3 hours = 0.12. */
    CHECK_NEAR(plant_health(&plant), 0.88f, 0.01);
    PlantConditions warm = at_temperature(plant_def(PLANT_PLANTAIN)->t_opt_low);
    plant_advance(&plant, &warm, HOURS_PER_DAY);
    CHECK(plant_health(&plant) > 0.95f);
    plant_advance(&plant, &warm, 3 * HOURS_PER_DAY);
    CHECK_NEAR(plant_health(&plant), 1.0f, 1e-6);
    /* A sick plant grows slower in proportion to its health. */
    plant.damage = PLANT_DAMAGE_MAX / 2;
    CHECK_NEAR(plant_growth_rate(&plant, &warm),
               0.5f * plant_def(PLANT_PLANTAIN)->growth_per_day * warm.fertility, 1e-3);
}

/* A tree with a trunk takes harm from the weather but never dies of it; a
 * seedling dies like any plant. */
static void test_trees_with_a_trunk_survive(void)
{
    Plant tree = {.type = PLANT_TREE, .growth = 0.8f};
    plant_settle(&tree, SEASON_DAY(SEASON_SUMMER, 5));
    PlantConditions furnace = at_temperature(plant_def(PLANT_TREE)->t_lethal_heat + 10.0f);
    plant_advance(&tree, &furnace, 10 * HOURS_PER_DAY);
    CHECK_EQ_INT(tree.type, PLANT_TREE);
    CHECK(plant_health(&tree) >= PLANT_TREE_HEALTH_FLOOR - 1e-3f);
    CHECK(plant_health(&tree) < 0.1f);
    Plant seedling = {.type = PLANT_TREE, .growth = 0.2f};
    plant_settle(&seedling, SEASON_DAY(SEASON_SUMMER, 5));
    plant_advance(&seedling, &furnace, 10 * HOURS_PER_DAY);
    CHECK_EQ_INT(seedling.type, PLANT_NONE);
}

/* How much light reaches a tile through what lies over it. */
static void test_light_cover(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 5));
    CHECK_NEAR(map_light_cover(&map, 3, 3, 0), 1.0, 1e-6); /* The open sky. */
    CHECK_NEAR(map_light_cover(&map, -1, 3, 0), 0.0, 1e-6);
    /* Foliage lets part of it through, by density, each layer in turn: the more
     * layers, the darker. */
    map_set_terrain(&map, 4, 3, 2, TERRAIN_FOLIAGE_SPARSE);
    CHECK_NEAR(map_light_cover(&map, 4, 3, 0), MAP_LIGHT_FOLIAGE_SPARSE, 1e-6);
    map_set_terrain(&map, 4, 3, 3, TERRAIN_FOLIAGE_MEDIUM);
    CHECK_NEAR(map_light_cover(&map, 4, 3, 0), MAP_LIGHT_FOLIAGE_SPARSE * MAP_LIGHT_FOLIAGE_MEDIUM,
               1e-6);
    map_set_terrain(&map, 4, 3, 1, TERRAIN_FOLIAGE);
    CHECK_NEAR(map_light_cover(&map, 4, 3, 0),
               MAP_LIGHT_FOLIAGE_SPARSE * MAP_LIGHT_FOLIAGE_MEDIUM * MAP_LIGHT_FOLIAGE_DENSE, 1e-6);
    CHECK(map_light_cover(&map, 4, 3, 1) >
          map_light_cover(&map, 4, 3, 0)); /* The top is lighter. */
    /* Every change of what is over a tile is counted for the caches of it. */
    uint32_t revision = map.light_revision;
    map_set_terrain(&map, 4, 3, 4, TERRAIN_FOLIAGE_SPARSE);
    CHECK(map.light_revision != revision);
    revision = map.light_revision;
    map_set_terrain(&map, 4, 3, 4, TERRAIN_FOLIAGE_SPARSE); /* Nothing changed. */
    CHECK_EQ_UINT(map.light_revision, revision);
    map_set_rock(&map, 9, 9, 4, ROCK_GRANITE);
    CHECK(map.light_revision != revision);
    /* The trunk of the tree over its own foot is not a roof. */
    map_set_rock(&map, 5, 3, 1, ROCK_WOOD);
    map_set_rock(&map, 5, 3, 2, ROCK_WOOD);
    CHECK_NEAR(map_light_cover(&map, 5, 3, 0), 1.0, 1e-6);
    /* A built roof lets a little through, rock or the ground above nothing. */
    map_set_roof(&map, 6, 3, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK_NEAR(map_light_cover(&map, 6, 3, 0), MAP_LIGHT_ROOF, 1e-6);
    map_set_terrain(&map, 7, 3, 1, TERRAIN_SOIL);
    CHECK_NEAR(map_light_cover(&map, 7, 3, 0), 0.0, 1e-6);
    map_set_rock(&map, 8, 3, 2, ROCK_GRANITE);
    CHECK_NEAR(map_light_cover(&map, 8, 3, 0), 0.0, 1e-6);
    map_free(&map);
}

/* The response to light: none below the plant's minimum, full from its light
 * level, in proportion between; a shade plant is fed where a sun plant starves. */
static void test_light_response(void)
{
    const PlantDef *grass = plant_def(PLANT_GRASS), *garlic = plant_def(PLANT_WILD_GARLIC);
    CHECK_NEAR(plant_light_response(PLANT_GRASS, 0.0f), 0.0, 1e-9);
    CHECK_NEAR(plant_light_response(PLANT_GRASS, grass->light_min), 0.0, 1e-9);
    CHECK_NEAR(plant_light_response(PLANT_GRASS, grass->light_full), 1.0, 1e-9);
    CHECK_NEAR(plant_light_response(PLANT_GRASS, 0.5f * (grass->light_min + grass->light_full)),
               0.5, 1e-6);
    CHECK_NEAR(plant_light_response(PLANT_GRASS, 1.0f), 1.0, 1e-9);
    CHECK_NEAR(plant_light_response(PLANT_WILD_GARLIC, garlic->light_full), 1.0, 1e-9);
    CHECK(plant_light_response(PLANT_WILD_GARLIC, 0.3f) >
          plant_light_response(PLANT_GRASS, 0.3f)); /* Shade suits the garlic. */
    CHECK_NEAR(plant_light_response(PLANT_MUSHROOMS, 0.0f), 1.0, 1e-9); /* Fungi: none needed. */
    CHECK_NEAR(plant_light_response(PLANT_COUNT, 1.0f), 0.0, 1e-9);
}

/* The light makes growth: none at night (the best day is twelve hours of full
 * light), proportional to the response by day. */
static void test_growth_follows_the_light(void)
{
    const PlantDef *def = plant_def(PLANT_GRASS);
    PlantConditions day = at_temperature(def->t_opt_low);
    Plant grass = {.type = PLANT_GRASS, .growth = 0.3f};
    plant_settle(&grass, SEASON_DAY(SEASON_SUMMER, 5));
    PlantConditions night = day;
    night.light = 0.0f; /* The sun is down, but the day's best light is fine. */
    plant_advance(&grass, &night, 6);
    CHECK_NEAR(grass.growth, 0.3f, 1e-9);
    CHECK_NEAR(plant_health(&grass), 1.0f, 1e-9); /* And no harm done. */
    PlantConditions dim = day;
    dim.light = 0.5f * (def->light_min + def->light_full);
    plant_advance(&grass, &dim, 1);
    CHECK_NEAR(grass.growth,
               0.3f + 0.5f * def->growth_per_day / PLANT_BEST_DAY_HOURS *
                          terrain_def(TERRAIN_RICH_SOIL)->fertility,
               1e-6);
    CHECK_EQ_INT(plant_status(&grass, &day), PLANT_STATUS_GROWING);
    PlantConditions shade = day;
    shade.light_peak = 0.5f * (def->light_min + def->light_full);
    CHECK_EQ_INT(plant_status(&grass, &shade), PLANT_STATUS_SLOWED_DARK);
    shade.light_peak = def->light_min * 0.5f;
    CHECK_EQ_INT(plant_status(&grass, &shade), PLANT_STATUS_TOO_DARK);
}

/* A plant kept in the dark dies after its `dark_days`, a plant that rests
 * (it needs no light) or a fungus does not. */
static void test_the_dark_kills(void)
{
    const PlantDef *def = plant_def(PLANT_GRASS);
    PlantConditions dark = at_temperature(def->t_opt_low);
    dark.light = 0.0f;
    dark.light_peak = def->light_min * 0.5f;
    Plant grass = {.type = PLANT_GRASS, .growth = 0.6f};
    plant_settle(&grass, SEASON_DAY(SEASON_SUMMER, 5));
    plant_advance(&grass, &dark, (int)(def->dark_days - 1.0f) * HOURS_PER_DAY);
    CHECK_EQ_INT(grass.type, PLANT_GRASS);
    CHECK(plant_health(&grass) < 0.15f);
    plant_advance(&grass, &dark, 2 * HOURS_PER_DAY);
    CHECK_EQ_INT(grass.type, PLANT_NONE);
    /* A short dark spell is mended: a few cloudy days. */
    Plant mended = {.type = PLANT_GRASS, .growth = 0.6f};
    plant_settle(&mended, SEASON_DAY(SEASON_SUMMER, 5));
    plant_advance(&mended, &dark, 3 * HOURS_PER_DAY);
    CHECK(plant_health(&mended) < 0.8f);
    PlantConditions bright = at_temperature(def->t_opt_low);
    plant_advance(&mended, &bright, 5 * HOURS_PER_DAY);
    CHECK_NEAR(plant_health(&mended), 1.0f, 1e-3);
    /* Resting plants wait for the light; fungi live without. */
    Plant resting = {.type = PLANT_GRASS, .growth = 0.6f};
    plant_settle(&resting, SEASON_DAY(SEASON_WINTER, 8));
    PlantConditions winter = dark;
    winter.temperature = -3.0f;
    plant_advance(&resting, &winter, 40 * HOURS_PER_DAY);
    CHECK_EQ_INT(resting.type, PLANT_GRASS);
    CHECK_NEAR(plant_health(&resting), 1.0f, 1e-6);
    Plant mushrooms = {.type = PLANT_MUSHROOMS, .growth = 1.0f};
    plant_settle(&mushrooms, SEASON_DAY(SEASON_AUTUMN, 3));
    PlantConditions black = at_temperature(10.0f);
    black.light = black.light_peak = 0.0f;
    plant_advance(&mushrooms, &black, 20 * HOURS_PER_DAY);
    CHECK_EQ_INT(mushrooms.type, PLANT_MUSHROOMS);
    CHECK_NEAR(plant_health(&mushrooms), 1.0f, 1e-6);
}

/* Mushrooms go once their food is gone: picked, or when the autumn turns
 * cold; a hard frost kills them at once. */
static void test_mushrooms_vanish(void)
{
    Plant patch = {.type = PLANT_MUSHROOMS, .growth = 1.0f};
    plant_settle(&patch, SEASON_DAY(SEASON_AUTUMN, 3));
    REQUIRE(plant_has_food(&patch));
    advance_days(&patch, SEASON_DAY(SEASON_AUTUMN, 3), SEASON_DAY(SEASON_AUTUMN, 8));
    CHECK_EQ_INT(patch.type, PLANT_MUSHROOMS);
    advance_days(&patch, SEASON_DAY(SEASON_AUTUMN, 9), SEASON_DAY(SEASON_WINTER, 8));
    CHECK_EQ_INT(patch.type, PLANT_NONE); /* The cold of winter: gone. */

    Plant frozen = {.type = PLANT_MUSHROOMS, .growth = 1.0f};
    plant_settle(&frozen, SEASON_DAY(SEASON_AUTUMN, 3));
    PlantConditions frost = {
        .temperature = -6.0f, .fertility = 1.0f, .light = 1.0f, .light_peak = 1.0f};
    plant_advance(&frozen, &frost, 14);
    CHECK_EQ_INT(frozen.type, PLANT_NONE); /* The first hard frost. */
}

/* Fruit and leaves ripen with the warmth gathered since the plant woke, not on
 * a date: a warm spring brings them early, a cold one late; they are lost
 * when the autumn chill comes (bramble) or when the leaves turn tough (nettle). */
static void test_products_ripen_with_the_warmth(void)
{
    const PlantProduct *berries = plant_product(PLANT_BRAMBLE, 0);
    REQUIRE(berries != NULL);
    /* A bramble that woke after a cold winter: nothing until the warmth has come. */
    PlantConditions warm = {
        .temperature = 25.0f, .fertility = 1.0f, .light = 1.0f, .light_peak = 1.0f};
    PlantConditions cool = warm;
    cool.temperature = 12.0f;
    Plant early = {.type = PLANT_BRAMBLE, .growth = 1.0f};
    Plant late = {.type = PLANT_BRAMBLE, .growth = 1.0f};
    plant_settle(&early, SEASON_DAY(SEASON_SPRING, 2));
    plant_settle(&late, SEASON_DAY(SEASON_SPRING, 2));
    REQUIRE(!plant_has_food(&early));
    int days_early = 0, days_late = 0;
    while (!plant_has_food(&early) && days_early < 400) {
        plant_advance(&early, &warm, HOURS_PER_DAY);
        days_early++;
    }
    while (!plant_has_food(&late) && days_late < 400) {
        plant_advance(&late, &cool, HOURS_PER_DAY);
        days_late++;
    }
    CHECK(days_early < 400 && days_late < 400);
    CHECK(days_early * 2 < days_late); /* A warm spell ripens it much sooner. */
    CHECK(early.warmth >= berries->warmth_from);
    /* The autumn chill spoils what is on it. */
    PlantConditions chill = cool;
    chill.temperature = berries->lost_below - 4.0f;
    plant_advance(&early, &chill, 5 * HOURS_PER_DAY);
    CHECK(!plant_has_food(&early));
    CHECK(!plant_product_in_season(&early, 0));
    /* Waking again after a rest: the warmth starts over. */
    Plant plant = {.type = PLANT_BRAMBLE, .growth = 1.0f};
    plant_settle(&plant, GAME_START_DAY);
    CHECK(plant_has_food(&plant));
    float gathered = plant.warmth;
    CHECK(gathered > berries->warmth_from);
    PlantConditions cold = warm;
    cold.temperature = -2.0f;
    plant_advance(&plant, &cold, 8 * HOURS_PER_DAY); /* To rest. */
    CHECK_NEAR(plant.warmth, gathered, 1e-4);        /* What was gathered is kept. */
    plant_advance(&plant, &warm, 3 * HOURS_PER_DAY); /* And wakes. */
    CHECK(plant.warmth < gathered);
    CHECK(!plant_product_in_season(&plant, 0));
    /* Nettle shoots turn tough as the plant warms. */
    Plant nettle = {.type = PLANT_NETTLE, .growth = 0.9f};
    plant_settle(&nettle, SEASON_DAY(SEASON_SPRING, 4));
    CHECK(plant_product_in_season(&nettle, 0));
    plant_advance(&nettle, &warm, 6 * HOURS_PER_DAY);
    CHECK(!plant_product_in_season(&nettle, 0));
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
    CHECK(map_ladder_fits(&map, 3, 2, 0));  /* At the foot of the hill. */
    CHECK(!map_ladder_fits(&map, 1, 2, 0)); /* Plains all around. */
    CHECK(!map_ladder_fits(&map, 6, 6, 0)); /* In the room: ground over it. */
    CHECK(!map_ladder_fits(&map, 3, 2, 2)); /* The top level. */
    map_set_roof(&map, 2, 2, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK(!map_ladder_fits(&map, 1, 2, 0)); /* Nobody climbs onto a roof. */
    map_set_roof(&map, 2, 2, 1, STRUCTURE_NONE, ITEM_QUALITY_STANDARD);
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
    /* A ladder is no ramp. */
    CHECK_EQ_INT(map_ramp_at(&map, 3, 2, 0), MAP_RAMP_NONE);
    CHECK_EQ_INT(map_ramp_at(&map, 3, 2, 1), MAP_RAMP_NONE);
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
    CHECK(map_ceiling_supported(&map, 2, 5, 0));  /* Nothing to hold up. */
    CHECK(!map_ceiling_supported(&map, 7, 7, 0)); /* 3 tiles from the rock. */
    CHECK(map_ceiling_supported(&map, 7, 5, 0));

    /* A prop holds up what is within reach, its own tile included. */
    map_set_structure(&map, 7, 7, 0, STRUCTURE_PROP);
    CHECK(map_ceiling_supported(&map, 7, 7, 0));
    CHECK(map_dig_keeps_ceilings(&map, 10, 7, 0, false)); /* Widening the room. */
    map_set_structure(&map, 7, 7, 0, STRUCTURE_NONE);

    /* Digging: the rock dug out holds nothing up any more. */
    map_set_rock(&map, 7, 7, 0, ROCK_EARTH);              /* A pillar left in the middle. */
    CHECK(map_dig_keeps_ceilings(&map, 4, 2, 0, false));  /* Away from the room. */
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

    /* A built roof is the ceiling of the tile under it: on the plains, held
     * up by a lone boulder of earth only, which may not be dug out. */
    map_set_rock(&map, 1, 12, 0, ROCK_EARTH);
    map_set_roof(&map, 1, 14, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK(map_has_ceiling(&map, 1, 14, 0));
    CHECK(map_ceiling_supported(&map, 1, 14, 0));
    CHECK(map_roof_supported(&map, 1, 14, 1));
    CHECK(!map_dig_keeps_ceilings(&map, 1, 12, 0, false));
    map_set_structure(&map, 0, 15, 0, STRUCTURE_WALL);
    CHECK(map_dig_keeps_ceilings(&map, 1, 12, 0, false));
    map_free(&map);
}

static void test_change_logs(void)
{
    Map map;
    REQUIRE(map_init(&map, 20, 16, 2));
    /* Structures and roofs: the build log. */
    uint32_t build = map.build_revision;
    map_set_structure(&map, 3, 4, 0, STRUCTURE_WALL);
    CHECK_EQ_UINT(map.build_revision, build + 1);
    const MapBuildChange *change = &map.build_log[map.build_revision % MAP_BUILD_LOG_SIZE];
    CHECK_EQ_INT(change->x, 3);
    CHECK_EQ_INT(change->y, 4);
    map_set_roof(&map, 7, 8, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    change = &map.build_log[map.build_revision % MAP_BUILD_LOG_SIZE];
    CHECK_EQ_INT(change->x, 7);
    CHECK_EQ_INT(change->y, 8);
    CHECK_EQ_INT(change->z, 1);
    /* Rock on a wall: nobody could walk there anyway, but it is a change. */
    uint32_t topology = map.topology_revision;
    build = map.build_revision;
    map_set_rock(&map, 3, 4, 0, ROCK_GRANITE);
    CHECK_EQ_UINT(map.topology_revision, topology);
    CHECK_EQ_UINT(map.build_revision, build + 1);
    /* Rock on open ground: the topology log, on its tile. */
    map_set_rock(&map, 10, 10, 0, ROCK_GRANITE);
    const MapTopologyChange *moved =
        &map.topology_log[map.topology_revision % MAP_TOPOLOGY_LOG_SIZE];
    CHECK_EQ_INT(moved->x, 10);
    CHECK_EQ_INT(moved->y, 10);
    CHECK(!moved->opened && !moved->anywhere);
    /* Layers written directly: anywhere. */
    map_topology_changed(&map);
    moved = &map.topology_log[map.topology_revision % MAP_TOPOLOGY_LOG_SIZE];
    CHECK(moved->anywhere);
    map_free(&map);
}

/* Branches and twigs lying on tiles: at most MAP_*_MAX of each, taken all at once. */
static void test_litter(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 2));
    CHECK_EQ_INT(map_litter(&map, 3, 3, 1, ITEM_TWIGS), 0);
    uint64_t empty = map_hash(&map);
    for (int i = 0; i < MAP_TWIGS_MAX; i++) {
        CHECK(map_add_litter(&map, 3, 3, 1, ITEM_TWIGS));
    }
    CHECK(!map_add_litter(&map, 3, 3, 1, ITEM_TWIGS)); /* Full. */
    CHECK_EQ_INT(map_litter(&map, 3, 3, 1, ITEM_TWIGS), MAP_TWIGS_MAX);
    CHECK_EQ_INT(map_litter(&map, 3, 3, 1, ITEM_BRANCHES), 0); /* Kept apart. */
    CHECK_EQ_INT(map_litter(&map, 3, 3, 0, ITEM_TWIGS), 0);
    for (int i = 0; i < MAP_BRANCHES_MAX; i++) {
        CHECK(map_add_litter(&map, 3, 3, 1, ITEM_BRANCHES));
    }
    CHECK(!map_add_litter(&map, 3, 3, 1, ITEM_BRANCHES));
    CHECK(map_hash(&map) != empty);
    CHECK(!map_add_litter(&map, 3, 3, 1, ITEM_WOOD)); /* Not litter. */
    CHECK(!map_add_litter(&map, -1, 3, 1, ITEM_TWIGS));
    CHECK_EQ_INT(map_litter(&map, 16, 3, 1, ITEM_TWIGS), 0);
    CHECK_EQ_INT(map_take_litter(&map, 3, 3, 1, ITEM_TWIGS), MAP_TWIGS_MAX);
    CHECK_EQ_INT(map_take_litter(&map, 3, 3, 1, ITEM_TWIGS), 0);
    CHECK_EQ_INT(map_take_litter(&map, 3, 3, 1, ITEM_BRANCHES), MAP_BRANCHES_MAX);
    CHECK_EQ_INT(map_take_litter(&map, 3, 3, 1, ITEM_COBBLE), 0);
    CHECK(map_hash(&map) == empty);
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
    RUN_TEST(test_harvest_effects);
    RUN_TEST(test_grass_stock_follows_growth);
    RUN_TEST(test_edible_plants);
    RUN_TEST(test_water_and_drinking_spots);
    RUN_TEST(test_plant_growth_stages_and_fruit);
    RUN_TEST(test_plant_days);
    RUN_TEST(test_fruit_follows_the_season);
    RUN_TEST(test_plants_rest_by_the_climate);
    RUN_TEST(test_growth_follows_the_temperature);
    RUN_TEST(test_nothing_grows_in_winter);
    RUN_TEST(test_cold_puts_plants_to_rest);
    RUN_TEST(test_dying_back_spares_the_roots);
    RUN_TEST(test_frost_and_heat_kill);
    RUN_TEST(test_hardening_takes_days);
    RUN_TEST(test_plants_mend);
    RUN_TEST(test_trees_with_a_trunk_survive);
    RUN_TEST(test_light_cover);
    RUN_TEST(test_light_response);
    RUN_TEST(test_growth_follows_the_light);
    RUN_TEST(test_the_dark_kills);
    RUN_TEST(test_mushrooms_vanish);
    RUN_TEST(test_products_ripen_with_the_warmth);
    RUN_TEST(test_tree_near);
    RUN_TEST(test_ladders);
    RUN_TEST(test_ceilings_need_support);
    RUN_TEST(test_change_logs);
    RUN_TEST(test_litter);
    return test_summary();
}
