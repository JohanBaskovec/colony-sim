#include "core/designation.h"
#include "core/i18n.h"
#include "core/util.h"

#include "test_framework.h"

/* 16 x 16 soil map with a tree at (3, 3), a granite wall at (5, 5) and a
 * sandstone wall at (6, 5). Trees are felled in gathering zones, not with
 * designations. */
static bool init_world(Map *map, Designations *designations)
{
    if (!map_init(map, 16, 16) || !designations_init(designations, 16, 16)) {
        return false;
    }
    Plant *tree = &map->plants[map_index(map, 3, 3)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    plant_settle(tree, GAME_START_DAY);
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
    CHECK(!designation_can_apply(&map, 3, 3, DESIGNATION_MINE));
    CHECK(designation_can_apply(&map, 5, 5, DESIGNATION_MINE));
    CHECK(!designation_can_apply(&map, -1, 5, DESIGNATION_MINE));
    CHECK(!designation_can_apply(&map, 5, 5, DESIGNATION_NONE));
    CHECK(!designation_can_apply(&map, 5, 5, DESIGNATION_COUNT));

    CHECK(!designation_set(&d, &map, 8, 8, DESIGNATION_MINE)); /* Nothing to mine. */
    CHECK(!designation_set(&d, &map, 99, 5, DESIGNATION_MINE));
    CHECK(designation_set(&d, &map, 5, 5, DESIGNATION_MINE));
    CHECK(!designation_set(&d, &map, 5, 5, DESIGNATION_MINE)); /* Already there. */
    CHECK_EQ_INT(designation_get(&d, 5, 5), DESIGNATION_MINE);
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
    CHECK_EQ_INT(designations_set_rect(&d, &map, 20, 20, 6, 4, DESIGNATION_MINE), 1);
    CHECK_EQ_INT(designations_set_rect(&d, &map, -5, -5, 20, 20, DESIGNATION_MINE), 1);
    CHECK_EQ_INT(d.count, 2);
    CHECK_EQ_INT(designations_set_rect(&d, &map, 0, 0, 15, 15, DESIGNATION_NONE), 2);
    CHECK_EQ_INT(d.count, 0);
    free_world(&map, &d);
}

static void test_counts_per_work_type(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    designation_set(&d, &map, 5, 5, DESIGNATION_MINE);
    designation_set(&d, &map, 6, 5, DESIGNATION_MINE);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 2);
    CHECK_EQ_INT(designations_count_for_work(&d, WORK_TYPE_BIT(WORK_MINING)), 2);
    CHECK_EQ_INT(designations_count_for_work(
                     &d, WORK_TYPE_BIT(WORK_MINING) | WORK_TYPE_BIT(WORK_WOODCUTTING)),
                 2);
    CHECK_EQ_INT(designations_count_for_work(&d, WORK_TYPE_BIT(WORK_GATHERING)), 0);
    CHECK_EQ_INT(designations_count_for_work(&d, 0), 0);

    designation_add_work(&d, &map, NULL, 5, 5, rock_def(ROCK_GRANITE)->mine_work, 0, NULL);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 1);
    designation_set(&d, &map, 6, 5, DESIGNATION_NONE);
    CHECK_EQ_INT(d.count_by_work[WORK_MINING], 0);
    CHECK_EQ_INT(d.count, 0);
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

static void test_mining_drops_stone_blocks(void)
{
    Map map;
    Designations d;
    Items items;
    REQUIRE(init_world(&map, &d));
    REQUIRE(items_init(&items, 16, 16));
    designation_set(&d, &map, 5, 5, DESIGNATION_MINE);

    int count;
    CHECK_EQ_INT(designation_yield(&d, &map, 5, 5, &count), ITEM_STONE_BLOCK);
    CHECK_EQ_INT(count, rock_def(ROCK_GRANITE)->mined_yield);
    CHECK_EQ_INT(designation_yield(&d, &map, 0, 0, &count), ITEM_NONE);
    CHECK_EQ_INT(count, 0);

    designation_add_work(&d, &map, &items, 5, 5, 1e6f, 0, NULL);
    ItemStack stone = items_get(&items, 5, 5); /* Dropped where the rock was. */
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
    REQUIRE(items_init(&items, 16, 16));
    const double beginner_level = KNOWLEDGE_MASTERY_LEVEL;

    /* Stone blocks come out perfect whoever mines them. */
    REQUIRE(designation_set(&d, &map, 5, 5, DESIGNATION_MINE));
    designation_add_work(&d, &map, &items, 5, 5, 1e6f, beginner_level, NULL);
    ItemStack stone = items_get(&items, 5, 5);
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
    designation_set(&d, &map, 5, 5, DESIGNATION_MINE);
    uint64_t designated = designations_hash(&d, HASH_FNV1A64_INIT);
    CHECK(designated != empty);
    designation_add_work(&d, &map, NULL, 5, 5, 10.0f, 0, NULL);
    CHECK(designations_hash(&d, HASH_FNV1A64_INIT) != designated);
    free_world(&map, &d);
}

static void test_taking_structures_apart(void)
{
    Map map;
    Designations designations;
    Items items;
    REQUIRE(init_world(&map, &designations));
    REQUIRE(items_init(&items, 16, 16));
    CHECK(!designation_set(&designations, &map, 8, 8, DESIGNATION_DECONSTRUCT)); /* Nothing. */
    map_set_structure(&map, 8, 8, STRUCTURE_DOOR);
    CHECK(designation_can_apply(&map, 8, 8, DESIGNATION_DECONSTRUCT));
    CHECK(designation_set(&designations, &map, 8, 8, DESIGNATION_DECONSTRUCT));
    CHECK_EQ_INT(designations_count_for_work(&designations, WORK_TYPE_BIT(WORK_CONSTRUCTION)), 1);
    float needed = designation_work_needed(&designations, &map, 8, 8);
    CHECK_NEAR(needed, structure_deconstruct_work(STRUCTURE_DOOR), 0.001f);
    CHECK(!designation_add_work(&designations, &map, &items, 8, 8, needed * 0.5f, 75.0, NULL));
    CHECK(designation_add_work(&designations, &map, &items, 8, 8, needed * 0.5f, 75.0, NULL));
    /* Gone, with half of its materials (5 branches and 5 twigs) given back. */
    CHECK_EQ_INT(map_structure(&map, 8, 8), STRUCTURE_NONE);
    CHECK_EQ_INT(designation_get(&designations, 8, 8), DESIGNATION_NONE);
    CHECK_EQ_INT(items.totals[ITEM_BRANCHES], 2);
    CHECK_EQ_INT(items.totals[ITEM_TWIGS], 2);
    items_free(&items);
    free_world(&map, &designations);
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
    /* Mining belongs to nobody yet. */
    CHECK(designation_def(DESIGNATION_MINE)->knowledge == KNOWLEDGE_NONE);
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
    RUN_TEST(test_definitions);
    return test_summary();
}
