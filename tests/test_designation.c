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
    plant_fill_stocks(tree);
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

    designation_add_work(&d, &map, NULL, 5, 5, rock_def(ROCK_GRANITE)->mine_work, 0, NULL);
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

    CHECK(!designation_add_work(&d, &map, NULL, 3, 3, needed * 0.5f, 0, NULL));
    CHECK_NEAR(designation_progress_fraction(&d, &map, 3, 3), 0.5, 1e-4);
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3)].type, PLANT_TREE);

    uint32_t revision = d.revision;
    CHECK(designation_add_work(&d, &map, NULL, 3, 3, needed * 0.5f, 0, NULL));
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3)].type, PLANT_NONE);
    CHECK_EQ_INT(designation_get(&d, 3, 3), DESIGNATION_NONE);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 3, 3), 0.0, 1e-6);
    CHECK_EQ_INT(d.count, 0);
    CHECK(d.revision != revision);
    CHECK(!designation_add_work(&d, &map, NULL, 3, 3, 1.0f, 0, NULL)); /* Nothing left to do. */
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
    CHECK(designation_add_work(&d, &map, NULL, 5, 5, rock_def(ROCK_GRANITE)->mine_work, 0, NULL));
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

    designation_add_work(&d, &map, &items, 3, 3, 1e6f, 0, NULL);
    ItemStack wood = items_get(&items, 3, 3);
    CHECK_EQ_INT(wood.type, ITEM_WOOD);
    /* A full tile, the rest spread around. */
    CHECK_EQ_INT(wood.count, item_stack_limit(ITEM_WOOD));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], plant_def(PLANT_TREE)->cut_yield);

    designation_add_work(&d, &map, &items, 5, 5, 1e6f, 0, NULL);
    ItemStack stone = items_get(&items, 5, 5); /* Dropped where the rock was. */
    CHECK_EQ_INT(stone.type, ITEM_STONE_BLOCK);
    CHECK_EQ_INT(stone.count, rock_def(ROCK_GRANITE)->mined_yield);
    items_free(&items);
    free_world(&map, &d);
}

static void test_products_depend_on_level(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16));
    int wood_hp = item_def(ITEM_WOOD)->max_hit_points;
    const double beginner_level = KNOWLEDGE_MASTERY_LEVEL;

    /* A beginner brings back rough, damaged logs... */
    REQUIRE(designation_set(&d, &map, 3, 3, DESIGNATION_CHOP));
    designation_add_work(&d, &map, &items, 3, 3, 1e6f, beginner_level, NULL);
    ItemStack beginner = items_get(&items, 3, 3);
    CHECK_EQ_INT(beginner.type, ITEM_WOOD);
    CHECK_EQ_INT(beginner.quality,
                 knowledge_quality_of_score(knowledge_product_score(beginner_level, NULL)));
    CHECK_EQ_INT(beginner.hit_points,
                 item_hit_points_at_percent(ITEM_WOOD,
                                            knowledge_product_condition(beginner_level, NULL)));
    CHECK(beginner.hit_points < wood_hp);

    /* ...a master superb, intact ones. */
    Plant *tree = &map.plants[map_index(&map, 12, 12)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    REQUIRE(designation_set(&d, &map, 12, 12, DESIGNATION_CHOP));
    designation_add_work(&d, &map, &items, 12, 12, 1e6f, KNOWLEDGE_LEVEL_MAX, NULL);
    ItemStack master = items_get(&items, 12, 12);
    CHECK_EQ_INT(master.quality, ITEM_QUALITY_SUPERB);
    CHECK_EQ_INT(master.hit_points, wood_hp);

    /* Stone blocks come out perfect whoever mines them. */
    REQUIRE(designation_set(&d, &map, 5, 5, DESIGNATION_MINE));
    designation_add_work(&d, &map, &items, 5, 5, 1e6f, beginner_level, NULL);
    ItemStack stone = items_get(&items, 5, 5);
    CHECK_EQ_INT(stone.type, ITEM_STONE_BLOCK);
    CHECK_EQ_INT(stone.quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(stone.hit_points, item_def(ITEM_STONE_BLOCK)->max_hit_points);

    /* Gathered stones: a good eye picks stones that knap well. */
    REQUIRE(designation_set(&d, &map, 9, 9, DESIGNATION_GATHER_STONES));
    designation_add_work(&d, &map, &items, 9, 9, 1e6f, beginner_level, NULL);
    REQUIRE(designation_set(&d, &map, 10, 9, DESIGNATION_GATHER_STONES));
    designation_add_work(&d, &map, &items, 10, 9, 1e6f, KNOWLEDGE_LEVEL_MAX, NULL);
    CHECK(items_get(&items, 9, 9).quality <= ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(items_get(&items, 10, 9).quality, ITEM_QUALITY_SUPERB);
    items_free(&items);
    free_world(&map, &d);
}

static void test_gathering_orders_need_a_target(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    /* Stones lie on soil, gravel and rough stone, not on sand, water or in rock. */
    CHECK(designation_can_apply(&map, 8, 8, DESIGNATION_GATHER_STONES));
    map.terrain[map_index(&map, 9, 8)] = TERRAIN_GRAVEL;
    map.terrain[map_index(&map, 10, 8)] = TERRAIN_SAND;
    map.terrain[map_index(&map, 11, 8)] = TERRAIN_SHALLOW_WATER;
    CHECK(designation_can_apply(&map, 9, 8, DESIGNATION_GATHER_STONES));
    CHECK(!designation_can_apply(&map, 10, 8, DESIGNATION_GATHER_STONES));
    CHECK(!designation_can_apply(&map, 11, 8, DESIGNATION_GATHER_STONES));
    CHECK(!designation_can_apply(&map, 5, 5, DESIGNATION_GATHER_STONES));
    CHECK(designation_can_apply(&map, 3, 3, DESIGNATION_GATHER_STONES)); /* Under a tree too. */

    /* Branches on trees, twigs on trees and bushes, while they have some. */
    CHECK(designation_can_apply(&map, 3, 3, DESIGNATION_GATHER_BRANCHES));
    CHECK(designation_can_apply(&map, 3, 3, DESIGNATION_GATHER_TWIGS));
    CHECK(!designation_can_apply(&map, 8, 8, DESIGNATION_GATHER_BRANCHES));
    Plant *bush = &map.plants[map_index(&map, 12, 3)];
    bush->type = PLANT_BUSH;
    bush->growth = 1.0f;
    CHECK(!designation_can_apply(&map, 12, 3, DESIGNATION_GATHER_TWIGS)); /* Empty stock. */
    plant_fill_stocks(bush);
    CHECK(designation_can_apply(&map, 12, 3, DESIGNATION_GATHER_TWIGS));
    CHECK(!designation_can_apply(&map, 12, 3, DESIGNATION_GATHER_BRANCHES));

    /* Grass needs to be tall enough. */
    Plant *grass = &map.plants[map_index(&map, 12, 12)];
    grass->type = PLANT_GRASS;
    grass->growth = 0.4f;
    CHECK(!designation_can_apply(&map, 12, 12, DESIGNATION_CUT_GRASS));
    grass->growth = 0.5f;
    CHECK(designation_can_apply(&map, 12, 12, DESIGNATION_CUT_GRASS));
    CHECK(!designation_can_apply(&map, 3, 3, DESIGNATION_CUT_GRASS));
    free_world(&map, &d);
}

static void test_gathering_stones_repeats(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16));
    REQUIRE(designation_set(&d, &map, 8, 8, DESIGNATION_GATHER_STONES));
    CHECK(designation_def(DESIGNATION_GATHER_STONES)->repeats);
    float needed = designation_work_needed(&d, &map, 8, 8);
    CHECK(needed > 0.0f);

    CHECK(!designation_add_work(&d, &map, &items, 8, 8, needed * 0.5f, 60.0, NULL));
    CHECK(designation_add_work(&d, &map, &items, 8, 8, needed * 0.5f, 60.0, NULL));
    CHECK_EQ_INT(items_get(&items, 8, 8).type, ITEM_STONE);
    CHECK_EQ_INT(items_get(&items, 8, 8).count, 1);
    /* The order stays, from scratch: stones are never exhausted. */
    CHECK_EQ_INT(designation_get(&d, 8, 8), DESIGNATION_GATHER_STONES);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 8, 8), 0.0, 1e-6);
    CHECK_EQ_INT(d.count, 1);
    for (int i = 0; i < 9; i++) {
        CHECK(designation_add_work(&d, &map, &items, 8, 8, needed, 60.0, NULL));
    }
    CHECK_EQ_INT((int)items.totals[ITEM_STONE], 10);
    CHECK_EQ_INT(designation_get(&d, 8, 8), DESIGNATION_GATHER_STONES);
    items_free(&items);
    free_world(&map, &d);
}

static void test_gathering_branches_leaves_the_tree(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16));
    Plant *tree = &map.plants[map_index(&map, 3, 3)];
    int branches = plant_def(PLANT_TREE)->stock_capacity[PLANT_STOCK_BRANCHES];
    REQUIRE(designation_set(&d, &map, 3, 3, DESIGNATION_GATHER_BRANCHES));
    int count;
    CHECK_EQ_INT(designation_yield(&d, &map, 3, 3, &count), ITEM_BRANCHES);
    CHECK_EQ_INT(count, branches);
    /* The work grows with what there is to take. */
    float needed = designation_work_needed(&d, &map, 3, 3);
    tree->stock[PLANT_STOCK_BRANCHES] /= 2;
    CHECK(designation_work_needed(&d, &map, 3, 3) < needed);
    plant_fill_stocks(tree);

    CHECK(designation_add_work(&d, &map, &items, 3, 3, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_BRANCHES], branches);
    CHECK_EQ_INT(tree->type, PLANT_TREE);
    CHECK_EQ_INT(plant_stock_available(tree, PLANT_STOCK_BRANCHES), 0);
    CHECK(plant_stock_available(tree, PLANT_STOCK_TWIGS) > 0); /* Twigs are another stock. */
    CHECK_EQ_INT(designation_get(&d, 3, 3), DESIGNATION_NONE);
    CHECK(!designation_set(&d, &map, 3, 3, DESIGNATION_GATHER_BRANCHES)); /* Bare for now. */

    /* Branches grow back, one at a time. */
    plant_regrow_stocks(tree, plant_def(PLANT_TREE)->stock_regrow_hours[PLANT_STOCK_BRANCHES]);
    CHECK_EQ_INT(plant_stock_available(tree, PLANT_STOCK_BRANCHES), 1);
    CHECK(designation_set(&d, &map, 3, 3, DESIGNATION_GATHER_BRANCHES));

    /* Twigs the same way. */
    REQUIRE(designation_set(&d, &map, 3, 3, DESIGNATION_GATHER_TWIGS));
    CHECK(designation_add_work(&d, &map, &items, 3, 3, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_TWIGS],
                 plant_def(PLANT_TREE)->stock_capacity[PLANT_STOCK_TWIGS]);
    CHECK_EQ_INT(plant_stock_available(tree, PLANT_STOCK_TWIGS), 0);
    items_free(&items);
    free_world(&map, &d);
}

static void test_cutting_grass(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16));
    Plant *tall = &map.plants[map_index(&map, 10, 10)];
    tall->type = PLANT_GRASS;
    tall->growth = 1.0f;
    Plant *short_grass = &map.plants[map_index(&map, 12, 12)];
    short_grass->type = PLANT_GRASS;
    short_grass->growth = 0.5f;
    REQUIRE(designation_set(&d, &map, 10, 10, DESIGNATION_CUT_GRASS));
    REQUIRE(designation_set(&d, &map, 12, 12, DESIGNATION_CUT_GRASS));
    int count;
    CHECK_EQ_INT(designation_yield(&d, &map, 10, 10, &count), ITEM_FRESH_GRASS);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(designation_yield(&d, &map, 12, 12, &count), ITEM_FRESH_GRASS);
    CHECK_EQ_INT(count, 2);

    CHECK(designation_add_work(&d, &map, &items, 10, 10, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(items_get(&items, 10, 10).type, ITEM_FRESH_GRASS);
    CHECK_EQ_INT(items_get(&items, 10, 10).count, 3);
    CHECK_EQ_INT(tall->type, PLANT_GRASS); /* It grows back from the ground. */
    CHECK_NEAR(tall->growth, 0.0, 1e-6);
    CHECK_EQ_INT(designation_get(&d, 10, 10), DESIGNATION_NONE);
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

    float needed = designation_work_needed(&d, &map, 8, 8);
    CHECK(designation_add_work(&d, &map, &items, 8, 8, needed, 0, NULL));
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
    designation_add_work(&d, &map, NULL, 5, 5, 1000.0f, 0, NULL);
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
    designation_add_work(&d, &map, NULL, 3, 3, 10.0f, 0, NULL);
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
        CHECK(def->min_level >= KNOWLEDGE_MASTERY_LEVEL);
    }
    /* Gathering orders belong to Gathering; felling and mining to nobody yet. */
    CHECK(designation_def(DESIGNATION_GATHER_STONES)->knowledge == KNOWLEDGE_GATHERING);
    CHECK(designation_def(DESIGNATION_HARVEST)->knowledge == KNOWLEDGE_GATHERING);
    CHECK(designation_def(DESIGNATION_CHOP)->knowledge == KNOWLEDGE_NONE);
    CHECK(designation_def(DESIGNATION_MINE)->knowledge == KNOWLEDGE_NONE);
    CHECK(!designation_def(DESIGNATION_CUT_GRASS)->repeats);
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
    RUN_TEST(test_products_depend_on_level);
    RUN_TEST(test_gathering_orders_need_a_target);
    RUN_TEST(test_gathering_stones_repeats);
    RUN_TEST(test_gathering_branches_leaves_the_tree);
    RUN_TEST(test_cutting_grass);
    RUN_TEST(test_harvest_picks_ripe_fruit);
    RUN_TEST(test_progress_is_kept_until_cancelled);
    RUN_TEST(test_hash_detects_changes);
    RUN_TEST(test_definitions);
    return test_summary();
}
