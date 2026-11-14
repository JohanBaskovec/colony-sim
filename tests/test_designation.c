#include "core/designation.h"
#include "core/i18n.h"
#include "core/util.h"

#include "test_framework.h"

/* 16 x 16 soil map with a tree at (3, 3), a granite wall at (5, 5) and a
 * sandstone wall at (6, 5). */
static bool init_world(Map *map, Designations *designations)
{
    if (!map_init(map, 16, 16) || !designations_init(designations, 16, 16)) {
        return false;
    }
    Plant *tree = &map->plants[map_index(map, 3, 3)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    map->rock[map_index(map, 5, 5)] = ROCK_GRANITE;
    map->rock[map_index(map, 6, 5)] = ROCK_SANDSTONE;
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
    CHECK(!designations_init(&designations, 0, 10));
    CHECK(designations.type == NULL && designations.width == 0);
}

static void test_orders_need_a_valid_target(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    CHECK(designation_can_apply(&map, 3, 3, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, 3, 3, DESIGNATION_MINE));
    CHECK(designation_can_apply(&map, 5, 5, DESIGNATION_MINE));
    CHECK(!designation_can_apply(&map, 5, 5, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, 8, 8, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, -1, 3, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, 3, 3, DESIGNATION_NONE));

    CHECK(!designation_set(&d, &map, 8, 8, DESIGNATION_CHOP)); /* Nothing to chop. */
    CHECK(!designation_set(&d, &map, 99, 3, DESIGNATION_CHOP));
    CHECK(designation_set(&d, &map, 3, 3, DESIGNATION_CHOP));
    CHECK(!designation_set(&d, &map, 3, 3, DESIGNATION_CHOP)); /* Already there. */
    CHECK_EQ_INT(designation_get(&d, 3, 3), DESIGNATION_CHOP);
    CHECK_EQ_INT(designation_get(&d, 8, 8), DESIGNATION_NONE);
    CHECK_EQ_INT(designation_get(&d, -1, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(d.count, 1);
    free_world(&map, &d);
}

static void test_rectangle_only_marks_valid_tiles(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    /* Corners in any order, partly outside the map. */
    CHECK_EQ_INT(designations_set_rect(&d, &map, 20, 20, 4, 4, DESIGNATION_MINE), 2);
    CHECK_EQ_INT(designations_set_rect(&d, &map, -5, -5, 20, 20, DESIGNATION_CHOP), 1);
    CHECK_EQ_INT(d.count, 3);
    CHECK_EQ_INT(designations_set_rect(&d, &map, 0, 0, 15, 15, DESIGNATION_NONE), 3);
    CHECK_EQ_INT(d.count, 0);
    free_world(&map, &d);
}

static void test_counts_per_work_type(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    designation_set(&d, &map, 3, 3, DESIGNATION_CHOP);
    designation_set(&d, &map, 5, 5, DESIGNATION_MINE);
    designation_set(&d, &map, 6, 5, DESIGNATION_MINE);
    CHECK_EQ_INT(d.count_by_work[WORK_WOODCUTTING], 1);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 2);
    CHECK_EQ_INT(designations_count_for_work(&d, WORK_TYPE_BIT(WORK_MINING)), 2);
    CHECK_EQ_INT(designations_count_for_work(
                     &d, WORK_TYPE_BIT(WORK_MINING) | WORK_TYPE_BIT(WORK_WOODCUTTING)),
                 3);
    CHECK_EQ_INT(designations_count_for_work(&d, 0), 0);

    designation_add_work(&d, &map, NULL, 5, 5, rock_def(ROCK_GRANITE)->mine_work);
    designation_set(&d, &map, 3, 3, DESIGNATION_NONE);
    CHECK_EQ_INT(d.count_by_work[WORK_WOODCUTTING], 0);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 1);
    CHECK_EQ_INT(d.count, 1);
    free_world(&map, &d);
}

static void test_chopping_removes_the_tree(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    REQUIRE(designation_set(&d, &map, 3, 3, DESIGNATION_CHOP));
    float needed = designation_work_needed(&d, &map, 3, 3);
    CHECK(needed > 0.0f);

    CHECK(!designation_add_work(&d, &map, NULL, 3, 3, needed * 0.5f));
    CHECK_NEAR(designation_progress_fraction(&d, &map, 3, 3), 0.5, 1e-4);
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3)].type, PLANT_TREE);

    uint32_t revision = d.revision;
    CHECK(designation_add_work(&d, &map, NULL, 3, 3, needed * 0.5f));
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3)].type, PLANT_NONE);
    CHECK_EQ_INT(designation_get(&d, 3, 3), DESIGNATION_NONE);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 3, 3), 0.0, 1e-6);
    CHECK_EQ_INT(d.count, 0);
    CHECK(d.revision != revision);
    CHECK(!designation_add_work(&d, &map, NULL, 3, 3, 1.0f)); /* Nothing left to do. */
    free_world(&map, &d);
}

static void test_mining_opens_the_wall(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    REQUIRE(designation_set(&d, &map, 5, 5, DESIGNATION_MINE));
    REQUIRE(designation_set(&d, &map, 6, 5, DESIGNATION_MINE));
    /* Harder rock takes longer. */
    CHECK_NEAR(designation_work_needed(&d, &map, 5, 5), rock_def(ROCK_GRANITE)->mine_work, 1e-3);
    CHECK(designation_work_needed(&d, &map, 5, 5) > designation_work_needed(&d, &map, 6, 5));

    Terrain floor = (Terrain)map.terrain[map_index(&map, 5, 5)];
    CHECK(!map_is_passable(&map, 5, 5));
    CHECK(designation_add_work(&d, &map, NULL, 5, 5, rock_def(ROCK_GRANITE)->mine_work));
    CHECK(map_is_passable(&map, 5, 5));
    CHECK_EQ_INT(map.terrain[map_index(&map, 5, 5)], floor);
    CHECK_EQ_INT(d.count, 1);
    free_world(&map, &d);
}

static void test_completed_orders_drop_items(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16));
    /* A half-grown tree gives half the wood. */
    Plant *young = &map.plants[map_index(&map, 8, 8)];
    young->type = PLANT_TREE;
    young->growth = 0.5f;
    designation_set(&d, &map, 3, 3, DESIGNATION_CHOP);
    designation_set(&d, &map, 8, 8, DESIGNATION_CHOP);
    designation_set(&d, &map, 5, 5, DESIGNATION_MINE);

    int count;
    CHECK_EQ_INT(designation_yield(&d, &map, 3, 3, &count), ITEM_WOOD);
    CHECK_EQ_INT(count, plant_def(PLANT_TREE)->cut_yield);
    CHECK_EQ_INT(designation_yield(&d, &map, 8, 8, &count), ITEM_WOOD);
    CHECK_EQ_INT(count, (plant_def(PLANT_TREE)->cut_yield + 1) / 2);
    CHECK_EQ_INT(designation_yield(&d, &map, 0, 0, &count), ITEM_NONE);
    CHECK_EQ_INT(count, 0);

    designation_add_work(&d, &map, &items, 3, 3, 1e6f);
    ItemStack wood = items_get(&items, 3, 3);
    CHECK_EQ_INT(wood.type, ITEM_WOOD);
    /* A full tile, the rest spread around. */
    CHECK_EQ_INT(wood.count, item_stack_limit(ITEM_WOOD));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], plant_def(PLANT_TREE)->cut_yield);

    designation_add_work(&d, &map, &items, 5, 5, 1e6f);
    ItemStack stone = items_get(&items, 5, 5); /* Dropped where the rock was. */
    CHECK_EQ_INT(stone.type, ITEM_STONE);
    CHECK_EQ_INT(stone.count, rock_def(ROCK_GRANITE)->mined_yield);
    items_free(&items);
    free_world(&map, &d);
}

static void test_harvest_picks_ripe_fruit(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16));
    Plant *bush = &map.plants[map_index(&map, 8, 8)];
    bush->type = PLANT_BUSH;
    bush->growth = plant_def(PLANT_BUSH)->min_edible_growth - 0.1f;
    CHECK(!designation_set(&d, &map, 8, 8, DESIGNATION_HARVEST)); /* Not ripe yet. */
    CHECK(!designation_set(&d, &map, 3, 3, DESIGNATION_HARVEST)); /* Trees bear nothing. */
    bush->growth = 1.0f;
    REQUIRE(designation_set(&d, &map, 8, 8, DESIGNATION_HARVEST));
    CHECK_EQ_INT(d.count_by_work[WORK_GATHERING], 1);

    CHECK(designation_add_work(&d, &map, &items, 8, 8, designation_work_needed(&d, &map, 8, 8)));
    ItemStack berries = items_get(&items, 8, 8);
    CHECK_EQ_INT(berries.type, ITEM_BERRIES);
    CHECK_EQ_INT(berries.count, plant_def(PLANT_BUSH)->harvest_yield);
    CHECK_EQ_INT(bush->type, PLANT_BUSH); /* The bush stays and grows back. */
    CHECK_NEAR(bush->growth, PLANT_GROWTH_AFTER_PICKING, 1e-6);
    items_free(&items);
    free_world(&map, &d);
}

static void test_progress_is_kept_until_cancelled(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    REQUIRE(designation_set(&d, &map, 5, 5, DESIGNATION_MINE));
    designation_add_work(&d, &map, NULL, 5, 5, 1000.0f);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 5, 5),
               1000.0 / rock_def(ROCK_GRANITE)->mine_work, 1e-4);

    /* Cancelling the order forgets the progress. */
    designation_set(&d, &map, 5, 5, DESIGNATION_NONE);
    designation_set(&d, &map, 5, 5, DESIGNATION_MINE);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 5, 5), 0.0, 1e-6);
    free_world(&map, &d);
}

static void test_hash_detects_changes(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    uint64_t empty = designations_hash(&d, HASH_FNV1A64_INIT);
    designation_set(&d, &map, 3, 3, DESIGNATION_CHOP);
    uint64_t designated = designations_hash(&d, HASH_FNV1A64_INIT);
    CHECK(designated != empty);
    designation_add_work(&d, &map, NULL, 3, 3, 10.0f);
    CHECK(designations_hash(&d, HASH_FNV1A64_INIT) != designated);
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
    }
    CHECK(designation_def(DESIGNATION_COUNT) == NULL);
}

int main(void)
{
    RUN_TEST(test_init_rejects_invalid_sizes);
    RUN_TEST(test_orders_need_a_valid_target);
    RUN_TEST(test_rectangle_only_marks_valid_tiles);
    RUN_TEST(test_counts_per_work_type);
    RUN_TEST(test_chopping_removes_the_tree);
    RUN_TEST(test_mining_opens_the_wall);
    RUN_TEST(test_completed_orders_drop_items);
    RUN_TEST(test_harvest_picks_ripe_fruit);
    RUN_TEST(test_progress_is_kept_until_cancelled);
    RUN_TEST(test_hash_detects_changes);
    RUN_TEST(test_definitions);
    return test_summary();
}
