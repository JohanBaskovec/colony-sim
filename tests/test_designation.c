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
    CHECK_EQ_INT(d.available, 1);
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
    CHECK_EQ_INT(d.available, 0);
    free_world(&map, &d);
}

static void test_reservations(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    CHECK(!designation_reserve(&d, 3, 3, 1)); /* Not designated. */
    REQUIRE(designation_set(&d, &map, 3, 3, DESIGNATION_CHOP));
    uint32_t revision = d.revision;

    CHECK(!designation_reserve(&d, 3, 3, DESIGNATION_NO_OWNER));
    CHECK(designation_reserve(&d, 3, 3, 1));
    CHECK(!designation_is_available(&d, 3, 3));
    CHECK(!designation_reserve(&d, 3, 3, 2)); /* Two workers never share a tile. */
    CHECK_EQ_INT(designation_owner(&d, 3, 3), 1);
    CHECK_EQ_INT(d.available, 0);
    CHECK_EQ_UINT(d.revision, revision); /* Reserving makes nothing new available. */

    designation_release(&d, 3, 3, 2); /* Not the owner: ignored. */
    CHECK_EQ_INT(designation_owner(&d, 3, 3), 1);
    designation_release(&d, 3, 3, 1);
    CHECK(designation_is_available(&d, 3, 3));
    CHECK_EQ_INT(d.available, 1);
    CHECK(d.revision != revision);

    /* Removing a reserved order frees the reservation and keeps counters right. */
    REQUIRE(designation_reserve(&d, 3, 3, 1));
    CHECK(designation_set(&d, &map, 3, 3, DESIGNATION_NONE));
    CHECK_EQ_INT(designation_owner(&d, 3, 3), DESIGNATION_NO_OWNER);
    CHECK_EQ_INT(d.count, 0);
    CHECK_EQ_INT(d.available, 0);
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

    CHECK(!designation_add_work(&d, &map, 3, 3, needed * 0.5f));
    CHECK_NEAR(designation_progress_fraction(&d, &map, 3, 3), 0.5, 1e-4);
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3)].type, PLANT_TREE);

    uint32_t revision = d.revision;
    CHECK(designation_add_work(&d, &map, 3, 3, needed * 0.5f));
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3)].type, PLANT_NONE);
    CHECK_EQ_INT(designation_get(&d, 3, 3), DESIGNATION_NONE);
    CHECK_NEAR(designation_progress_fraction(&d, &map, 3, 3), 0.0, 1e-6);
    CHECK_EQ_INT(d.count, 0);
    CHECK(d.revision != revision);
    CHECK(!designation_add_work(&d, &map, 3, 3, 1.0f)); /* Nothing left to do. */
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
    CHECK(designation_add_work(&d, &map, 5, 5, rock_def(ROCK_GRANITE)->mine_work));
    CHECK(map_is_passable(&map, 5, 5));
    CHECK_EQ_INT(map.terrain[map_index(&map, 5, 5)], floor);
    CHECK_EQ_INT(d.count, 1);
    free_world(&map, &d);
}

static void test_progress_survives_reservation_changes(void)
{
    Map map;
    Designations d;
    REQUIRE(init_world(&map, &d));
    REQUIRE(designation_set(&d, &map, 5, 5, DESIGNATION_MINE));
    REQUIRE(designation_reserve(&d, 5, 5, 3));
    designation_add_work(&d, &map, 5, 5, 1000.0f);
    designation_release(&d, 5, 5, 3);
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
    designation_reserve(&d, 3, 3, 1);
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
    }
    CHECK(designation_def(DESIGNATION_COUNT) == NULL);
}

int main(void)
{
    RUN_TEST(test_init_rejects_invalid_sizes);
    RUN_TEST(test_orders_need_a_valid_target);
    RUN_TEST(test_rectangle_only_marks_valid_tiles);
    RUN_TEST(test_reservations);
    RUN_TEST(test_chopping_removes_the_tree);
    RUN_TEST(test_mining_opens_the_wall);
    RUN_TEST(test_progress_survives_reservation_changes);
    RUN_TEST(test_hash_detects_changes);
    RUN_TEST(test_definitions);
    return test_summary();
}
