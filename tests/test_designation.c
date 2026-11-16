#include "core/designation.h"
#include "core/i18n.h"
#include "core/util.h"

#include "test_framework.h"

/* 16 x 16 soil map with a tree at (3, 3), a granite wall at (5, 5) and a
 * sandstone wall at (6, 5). Trees are felled in gathering zones, not with
 * designations. */
static bool init_world(Map *map, Designations *designations)
{
    if (!map_init(map, 16, 16, 1) || !designations_init(designations, 16, 16, 1)) {
        return false;
    }
    Plant *tree = &map->plants[map_index(map, 3, 3, 0)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    plant_settle(tree, GAME_START_DAY);
    map->rock[map_index(map, 5, 5, 0)] = ROCK_GRANITE;
    map->rock[map_index(map, 6, 5, 0)] = ROCK_SANDSTONE;
    return true;
}

static void free_world(Map *map, Designations *designations)
{
    designations_free(designations);
    map_free(map);
}

static void test_init_rejects_invalid_sizes(void)
{
    Designations designations;
    CHECK(!designations_init(&designations, 0, 10, 1));
    CHECK(designations.type == NULL && designations.width == 0);
}

static void test_orders_need_a_valid_target(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    CHECK(!designation_can_apply(&map, 3, 3, 0, DESIGNATION_MINE));
    CHECK(designation_can_apply(&map, 5, 5, 0, DESIGNATION_MINE));
    CHECK(!designation_can_apply(&map, -1, 5, 0, DESIGNATION_MINE));
    CHECK(!designation_can_apply(&map, 5, 5, 0, DESIGNATION_NONE));
    CHECK(!designation_can_apply(&map, 5, 5, 0, DESIGNATION_COUNT));

    CHECK(!designation_set(&d, &map, 8, 8, 0, DESIGNATION_MINE)); /* Nothing to mine. */
    CHECK(!designation_set(&d, &map, 99, 5, 0, DESIGNATION_MINE));
    CHECK(designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE));
    CHECK(!designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE)); /* Already there. */
    CHECK_EQ_INT(designation_get(&d, 5, 5, 0), DESIGNATION_MINE);
    CHECK_EQ_INT(designation_get(&d, 8, 8, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(designation_get(&d, -1, 0, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(d.count, 1);
    free_world(&map, &d);
}

static void test_rectangle_only_marks_valid_tiles(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    /* Corners in any order, partly outside the map. */
    CHECK_EQ_INT(designations_set_rect(&d, &map, 20, 20, 6, 4, 0, DESIGNATION_MINE), 1);
    CHECK_EQ_INT(designations_set_rect(&d, &map, -5, -5, 20, 20, 0, DESIGNATION_MINE), 1);
    CHECK_EQ_INT(d.count, 2);
    CHECK_EQ_INT(designations_set_rect(&d, &map, 0, 0, 15, 15, 0, DESIGNATION_NONE), 2);
    CHECK_EQ_INT(d.count, 0);
    free_world(&map, &d);
}

static void test_counts_per_work_type(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE);
    designation_set(&d, &map, 6, 5, 0, DESIGNATION_MINE);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 2);
    CHECK_EQ_INT(designations_count_for_work(&d, WORK_TYPE_BIT(WORK_MINING)), 2);
    CHECK_EQ_INT(designations_count_for_work(
                     &d, WORK_TYPE_BIT(WORK_MINING) | WORK_TYPE_BIT(WORK_WOODCUTTING)),
                 2);
    CHECK_EQ_INT(designations_count_for_work(&d, WORK_TYPE_BIT(WORK_GATHERING)), 0);
    CHECK_EQ_INT(designations_count_for_work(&d, 0), 0);

    designation_add_work(&d, &map, NULL, 5, 5, 0, rock_def(ROCK_GRANITE)->mine_work, 0, NULL);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 1);
    designation_set(&d, &map, 6, 5, 0, DESIGNATION_NONE);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 0);
    CHECK_EQ_INT(d.count, 0);
    free_world(&map, &d);
}

static void test_mining_opens_the_wall(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    REQUIRE(designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE));
    REQUIRE(designation_set(&d, &map, 6, 5, 0, DESIGNATION_MINE));
    /* Harder rock takes longer. */
    CHECK_NEAR(designation_work_needed(&d, &map, 5, 5, 0), rock_def(ROCK_GRANITE)->mine_work, 1e-3);
    CHECK(designation_work_needed(&d, &map, 5, 5, 0) > designation_work_needed(&d, &map, 6, 5, 0));

    Terrain floor = (Terrain)map.terrain[map_index(&map, 5, 5, 0)];
    CHECK(!map_is_passable(&map, 5, 5, 0));
    CHECK(
        designation_add_work(&d, &map, NULL, 5, 5, 0, rock_def(ROCK_GRANITE)->mine_work, 0, NULL));
    CHECK(map_is_passable(&map, 5, 5, 0));
    CHECK_EQ_INT(map.terrain[map_index(&map, 5, 5, 0)], floor);
    CHECK_EQ_INT(d.count, 1);
    free_world(&map, &d);
}

static void test_mining_drops_stone_blocks(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16, 1));
    designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE);

    int count;
    CHECK_EQ_INT(designation_yield(&d, &map, 5, 5, 0, &count), ITEM_STONE_BLOCK);
    CHECK_EQ_INT(count, rock_def(ROCK_GRANITE)->mined_yield);
    CHECK_EQ_INT(designation_yield(&d, &map, 0, 0, 0, &count), ITEM_NONE);
    CHECK_EQ_INT(count, 0);

    designation_add_work(&d, &map, &items, 5, 5, 0, 1e6f, 0, NULL);
    ItemStack stone = items_get(&items, 5, 5, 0); /* Dropped where the rock was. */
    CHECK_EQ_INT(stone.type, ITEM_STONE_BLOCK);
    CHECK_EQ_INT(stone.count, rock_def(ROCK_GRANITE)->mined_yield);
    items_free(&items);
    free_world(&map, &d);
}

static void test_stone_blocks_are_perfect(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16, 1));
    const double beginner_level = KNOWLEDGE_MASTERY_LEVEL;

    /* Stone blocks come out perfect whoever mines them. */
    REQUIRE(designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE));
    designation_add_work(&d, &map, &items, 5, 5, 0, 1e6f, beginner_level, NULL);
    ItemStack stone = items_get(&items, 5, 5, 0);
    CHECK_EQ_INT(stone.type, ITEM_STONE_BLOCK);
    CHECK_EQ_INT(stone.quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(stone.hit_points, item_def(ITEM_STONE_BLOCK)->max_hit_points);
    items_free(&items);
    free_world(&map, &d);
}

static void test_progress_is_kept_until_cancelled(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    REQUIRE(designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE));
    designation_add_work(&d, &map, NULL, 5, 5, 0, 1000.0f, 0, NULL);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 5, 5, 0),
               1000.0 / rock_def(ROCK_GRANITE)->mine_work, 1e-4);

    /* Cancelling the order forgets the progress. */
    designation_set(&d, &map, 5, 5, 0, DESIGNATION_NONE);
    designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 5, 5, 0), 0.0, 1e-6);
    free_world(&map, &d);
}

static void test_hash_detects_changes(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    uint64_t empty = designations_hash(&d, HASH_FNV1A64_INIT);
    designation_set(&d, &map, 5, 5, 0, DESIGNATION_MINE);
    uint64_t designated = designations_hash(&d, HASH_FNV1A64_INIT);
    CHECK(designated != empty);
    designation_add_work(&d, &map, NULL, 5, 5, 0, 10.0f, 0, NULL);
    CHECK(designations_hash(&d, HASH_FNV1A64_INIT) != designated);
    free_world(&map, &d);
}

static void test_taking_structures_apart(void)
{
    Map map;
    Designations designations;
    Items items;
    REQUIRE(init_world(&map, &designations));
    REQUIRE(items_init(&items, 16, 16, 1));
    CHECK(!designation_set(&designations, &map, 8, 8, 0, DESIGNATION_DECONSTRUCT)); /* Nothing. */
    map_set_structure(&map, 8, 8, 0, STRUCTURE_DOOR);
    CHECK(designation_can_apply(&map, 8, 8, 0, DESIGNATION_DECONSTRUCT));
    CHECK(designation_set(&designations, &map, 8, 8, 0, DESIGNATION_DECONSTRUCT));
    CHECK_EQ_INT(designations_count_for_work(&designations, WORK_TYPE_BIT(WORK_CONSTRUCTION)), 1);
    float needed = designation_work_needed(&designations, &map, 8, 8, 0);
    CHECK_NEAR(needed, structure_deconstruct_work(STRUCTURE_DOOR), 0.001f);
    CHECK(!designation_add_work(&designations, &map, &items, 8, 8, 0, needed * 0.5f, 75.0, NULL));
    CHECK(designation_add_work(&designations, &map, &items, 8, 8, 0, needed * 0.5f, 75.0, NULL));
    /* Gone, with half of its materials (5 branches and 5 twigs) given back. */
    CHECK_EQ_INT(map_structure(&map, 8, 8, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(designation_get(&designations, 8, 8, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(items.totals[ITEM_BRANCHES], 2);
    CHECK_EQ_INT(items.totals[ITEM_TWIGS], 2);
    items_free(&items);
    free_world(&map, &designations);
}

/* 16 x 16, 3 levels: plains on level 0, a bank of earth for x >= 4 (earth
 * on level 0, its ground on level 1), open air above. */
static bool init_bank(Map *map, Designations *designations)
{
    if (!map_init(map, 16, 16, 3) || !designations_init(designations, 16, 16, 3)) {
        return false;
    }
    for (int y = 0; y < 16; y++) {
        for (int x = 4; x < 16; x++) {
            map->rock[map_index(map, x, y, 0)] = ROCK_EARTH;
            map->terrain[map_index(map, x, y, 1)] = TERRAIN_SOIL;
        }
    }
    map_topology_changed(map); /* The layers were written directly. */
    return true;
}

/* Earth is dug, rock is mined: two orders. */
static void test_earth_is_dug_rock_is_mined(void)
{
    Map map;
    Designations d;
    REQUIRE(init_bank(&map, &d));
    map.rock[map_index(&map, 6, 6, 0)] = ROCK_GRANITE;
    CHECK(designation_can_apply(&map, 4, 2, 0, DESIGNATION_DIG));
    CHECK(!designation_can_apply(&map, 4, 2, 0, DESIGNATION_MINE));
    CHECK(designation_can_apply(&map, 6, 6, 0, DESIGNATION_MINE));
    CHECK(!designation_can_apply(&map, 6, 6, 0, DESIGNATION_DIG));
    CHECK(!designation_can_apply(&map, 2, 2, 0, DESIGNATION_DIG)); /* Soil: nothing to dig. */

    /* A gallery into the bank: its ground stays overhead. */
    REQUIRE(designation_set(&d, &map, 4, 2, 0, DESIGNATION_DIG));
    float needed = designation_work_needed(&d, &map, 4, 2, 0);
    CHECK_NEAR(needed, rock_def(ROCK_EARTH)->mine_work, 1e-3);
    int count;
    CHECK_EQ_INT(designation_yield(&d, &map, 4, 2, 0, &count), ITEM_NONE);
    CHECK(designation_add_work(&d, &map, NULL, 4, 2, 0, needed, KNOWLEDGE_MASTERY_LEVEL, NULL));
    CHECK(map_is_passable(&map, 4, 2, 0));
    CHECK(map_has_floor(&map, 4, 2, 1));
    CHECK(map_natural_roof(&map, 4, 2, 0));
    CHECK_EQ_INT(designation_get(&d, 4, 2, 0), DESIGNATION_NONE);
    free_world(&map, &d);
}

/* A ramp cut into a bank leads up to the ground behind it. */
static void test_ramps_are_cut_into_banks(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_bank(&map, &d));
    REQUIRE(items_init(&items, 16, 16, 3));
    CHECK(designation_can_apply(&map, 4, 3, 0, DESIGNATION_DIG_RAMP));
    CHECK(!designation_can_apply(&map, 2, 3, 0, DESIGNATION_DIG_RAMP)); /* No bank. */
    CHECK(!designation_can_apply(&map, 4, 3, 1, DESIGNATION_DIG_RAMP));
    CHECK(!map_can_climb(&map, 3, 3, 0, 4, 3));

    /* What lies on the ground taken away slides down onto the ramp. */
    REQUIRE(items_add(&items, 4, 3, 1, item_stack_pristine(ITEM_WOOD, 3)) == 3);
    map.plants[map_index(&map, 4, 3, 1)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    REQUIRE(designation_set(&d, &map, 4, 3, 0, DESIGNATION_DIG_RAMP));
    float needed = designation_work_needed(&d, &map, 4, 3, 0);
    CHECK_NEAR(needed, rock_def(ROCK_EARTH)->mine_work * DESIGNATION_RAMP_WORK_FACTOR, 1e-3);
    CHECK(!designation_add_work(&d, &map, &items, 4, 3, 0, needed * 0.5f, 50.0, NULL));
    CHECK(designation_add_work(&d, &map, &items, 4, 3, 0, needed * 0.5f, 50.0, NULL));
    CHECK(map.rock[map_index(&map, 4, 3, 0)] == ROCK_NONE);
    CHECK(map_is_slope(&map, 4, 3, 0));
    CHECK(map_is_open(&map, 4, 3, 1));
    CHECK_EQ_INT(map.plants[map_index(&map, 4, 3, 1)].type, PLANT_NONE);
    CHECK(map_can_climb(&map, 4, 3, 0, 5, 3)); /* Up onto the bank. */
    CHECK_EQ_INT(items_get(&items, 4, 3, 1).count, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 3);
    CHECK_EQ_INT(map.slope_count, 1);
    items_free(&items);
    free_world(&map, &d);
}

/* No ramp where the ground over it holds something, has something over it,
 * or leads nowhere. */
static void test_ramps_need_bare_ground(void)
{
    Map map;
    Designations d;
    REQUIRE(init_bank(&map, &d));
    Plant *tree = &map.plants[map_index(&map, 4, 1, 1)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    plant_settle(tree, GAME_START_DAY);
    CHECK(!designation_can_apply(&map, 4, 1, 0, DESIGNATION_DIG_RAMP));
    map_set_structure(&map, 4, 2, 1, STRUCTURE_WALL);
    CHECK(!designation_can_apply(&map, 4, 2, 0, DESIGNATION_DIG_RAMP));
    map_set_terrain(&map, 4, 3, 2, TERRAIN_SOIL); /* A floor over it. */
    CHECK(!designation_can_apply(&map, 4, 3, 0, DESIGNATION_DIG_RAMP));
    map_set_slope(&map, 4, 4, 1, true);
    CHECK(!designation_can_apply(&map, 4, 4, 0, DESIGNATION_DIG_RAMP));
    /* A lone column of earth: no ground behind to lead up to. */
    map_set_terrain(&map, 5, 6, 1, TERRAIN_OPEN);
    map_set_terrain(&map, 4, 5, 1, TERRAIN_OPEN);
    map_set_terrain(&map, 4, 7, 1, TERRAIN_OPEN);
    CHECK(!designation_can_apply(&map, 4, 6, 0, DESIGNATION_DIG_RAMP));
    CHECK(designation_can_apply(&map, 5, 2, 0, DESIGNATION_DIG_RAMP)); /* Inside: fine. */

    /* Stockpiles, sites and stations live outside the map. */
    Constructions constructions;
    Workstations workstations;
    Stockpiles stockpiles;
    REQUIRE(constructions_init(&constructions, 16, 16, 3));
    REQUIRE(workstations_init(&workstations, 16, 16, 3));
    REQUIRE(stockpiles_init(&stockpiles, 16, 16, 3));
    CHECK(designation_ramp_ground_free(&constructions, &workstations, &stockpiles, 5, 2, 0));
    REQUIRE(stockpile_create(&stockpiles, &map, 5, 2, 5, 2, 1) != STOCKPILE_NONE);
    CHECK(!designation_ramp_ground_free(&constructions, &workstations, &stockpiles, 5, 2, 0));
    CHECK(designation_ramp_ground_free(&constructions, &workstations, &stockpiles, 5, 3, 0));
    stockpiles_free(&stockpiles);
    workstations_free(&workstations);
    constructions_free(&constructions);
    free_world(&map, &d);
}

static void test_definitions(void)
{
    for (int type = DESIGNATION_NONE + 1; type < DESIGNATION_COUNT; type++) {
        const DesignationDef *def = designation_def((DesignationType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(i18n_is_valid_key(def->activity_key));
        CHECK(work_type_def(def->work_type) != NULL);
        CHECK(def->min_level >= KNOWLEDGE_MASTERY_LEVEL);
    }
    /* Mining belongs to nobody yet; shelter builders dig earth, with a digging
     * stick if they have one. */
    CHECK(designation_def(DESIGNATION_MINE)->knowledge == KNOWLEDGE_NONE);
    CHECK(designation_def(DESIGNATION_DIG)->knowledge == KNOWLEDGE_BASIC_SHELTERS);
    CHECK(designation_def(DESIGNATION_DIG_RAMP)->knowledge == KNOWLEDGE_BASIC_SHELTERS);
    CHECK(designation_def(DESIGNATION_DIG)->tool_use == TOOL_USE_DIGGING);
    CHECK(designation_def(DESIGNATION_DECONSTRUCT)->tool_use == TOOL_USE_NONE);
    /* Taking a structure apart is construction work anyone who builds knows. */
    CHECK(designation_def(DESIGNATION_DECONSTRUCT)->work_type == WORK_CONSTRUCTION);
    CHECK(designation_def(DESIGNATION_DECONSTRUCT)->knowledge == KNOWLEDGE_BASIC_SHELTERS);
    CHECK(designation_def(DESIGNATION_COUNT) == NULL);
}

int main(void)
{
    RUN_TEST(test_init_rejects_invalid_sizes);
    RUN_TEST(test_orders_need_a_valid_target);
    RUN_TEST(test_rectangle_only_marks_valid_tiles);
    RUN_TEST(test_counts_per_work_type);
    RUN_TEST(test_mining_opens_the_wall);
    RUN_TEST(test_mining_drops_stone_blocks);
    RUN_TEST(test_stone_blocks_are_perfect);
    RUN_TEST(test_progress_is_kept_until_cancelled);
    RUN_TEST(test_hash_detects_changes);
    RUN_TEST(test_taking_structures_apart);
    RUN_TEST(test_earth_is_dug_rock_is_mined);
    RUN_TEST(test_ramps_are_cut_into_banks);
    RUN_TEST(test_ramps_need_bare_ground);
    RUN_TEST(test_definitions);
    return test_summary();
}
