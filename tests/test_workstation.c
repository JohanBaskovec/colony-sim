#include "core/workstation.h"
#include "core/util.h"

#include "test_framework.h"

#include <string.h>

/* 16 x 16 soil map with a granite wall at (5, 5) and water at (6, 6). */
static bool init_world(Map *map, Workstations *stations, Items *items)
{
    if (!map_init(map, 16, 16) || !workstations_init(stations, 16, 16) ||
        !items_init(items, 16, 16)) {
        return false;
    }
    map->rock[map_index(map, 5, 5)] = ROCK_GRANITE;
    map->terrain[map_index(map, 6, 6)] = TERRAIN_DEEP_WATER;
    return true;
}

static void free_world(Map *map, Workstations *stations, Items *items)
{
    items_free(items);
    workstations_free(stations);
    map_free(map);
}

static void test_definitions(void)
{
    for (int type = 0; type < WORKSTATION_TYPE_COUNT; type++) {
        const WorkstationDef *def = workstation_def((WorkstationType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL && def->name_key != NULL);
        CHECK((def->material == ITEM_NONE) == (def->material_count == 0));
        /* Only stations made of something need building. */
        CHECK((def->material == ITEM_NONE) == (def->build_work <= 0.0f));
    }
    CHECK(workstation_def(WORKSTATION_TYPE_COUNT) == NULL);
    /* Anyone can set up a crafting spot, with nothing: the chain can start. */
    CHECK_EQ_INT(workstation_def(WORKSTATION_CRAFTING_SPOT)->material, ITEM_NONE);
    CHECK_EQ_INT(workstation_def(WORKSTATION_CRAFTING_SPOT)->knowledge, KNOWLEDGE_NONE);
    CHECK_EQ_INT(workstation_def(WORKSTATION_KNAPPING_SPOT)->material, ITEM_COBBLE);
}

static void test_placing_and_removing(void)
{
    Map map;
    Workstations stations;
    Items items;
    REQUIRE(init_world(&map, &stations, &items));
    CHECK(!workstation_can_place(&stations, &map, 5, 5)); /* Rock. */
    CHECK(!workstation_can_place(&stations, &map, 6, 6)); /* Water. */
    CHECK(!workstation_can_place(&stations, &map, -1, 0));
    int index = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 3, 4);
    REQUIRE(index != WORKSTATION_NONE);
    CHECK_EQ_INT(workstation_at(&stations, 3, 4), index);
    CHECK_EQ_INT(workstation_at(&stations, 4, 4), WORKSTATION_NONE);
    CHECK(!workstation_can_place(&stations, &map, 3, 4)); /* Taken. */
    CHECK_EQ_INT(workstations_place(&stations, &map, WORKSTATION_CRAFTING_SPOT, 3, 4),
                 WORKSTATION_NONE);
    const Workstation *station = workstation_get_const(&stations, index);
    REQUIRE(station != NULL);
    CHECK(!station->built);
    CHECK_EQ_INT(workstation_materials_missing(station), 1);
    CHECK_EQ_INT(stations.used_count, 1);

    /* Materials delivered to a blueprint come back when it is removed. */
    uint32_t revision = stations.revision;
    CHECK_EQ_INT(workstation_deliver(&stations, index, item_stack_pristine(ITEM_WOOD, 3)), 0);
    CHECK_EQ_INT(workstation_deliver(&stations, index, item_stack_pristine(ITEM_COBBLE, 3)), 1);
    CHECK(stations.revision != revision);
    CHECK_EQ_INT(workstation_materials_missing(station), 0);
    ItemStack returned[2];
    int number = station->number;
    REQUIRE(workstations_remove(&stations, index, returned) == 1);
    CHECK_EQ_INT(returned[0].type, ITEM_COBBLE);
    CHECK_EQ_INT(returned[0].count, 1);
    CHECK(workstation_get(&stations, index) == NULL);
    CHECK_EQ_INT(workstation_at(&stations, 3, 4), WORKSTATION_NONE);
    CHECK_EQ_INT(stations.used_count, 0);
    CHECK_EQ_INT(workstations_remove(&stations, index, returned), 0);

    /* A new station never reuses a number. */
    index = workstations_place(&stations, &map, WORKSTATION_CRAFTING_SPOT, 3, 4);
    REQUIRE(index != WORKSTATION_NONE);
    CHECK(workstation_get_const(&stations, index)->number > number);
    free_world(&map, &stations, &items);
}

static void test_building(void)
{
    Map map;
    Workstations stations;
    Items items;
    REQUIRE(init_world(&map, &stations, &items));
    int index = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 3, 4);
    float needed = workstation_def(WORKSTATION_KNAPPING_SPOT)->build_work;
    /* No work before the materials are in. */
    CHECK(!workstation_add_build_work(&stations, index, needed));
    CHECK_NEAR(workstation_build_fraction(workstation_get_const(&stations, index)), 0.0, 1e-6);
    workstation_deliver(&stations, index, item_stack_pristine(ITEM_COBBLE, 1));
    CHECK(!workstation_add_build_work(&stations, index, needed * 0.5f));
    CHECK_NEAR(workstation_build_fraction(workstation_get_const(&stations, index)), 0.5, 1e-6);
    CHECK(workstation_add_build_work(&stations, index, needed * 0.5f));
    const Workstation *station = workstation_get_const(&stations, index);
    CHECK(station->built);
    CHECK_NEAR(workstation_build_fraction(station), 1.0, 1e-6);
    CHECK(!workstation_add_build_work(&stations, index, needed));
    /* The anvil stone is part of the station now. */
    ItemStack returned[2];
    CHECK_EQ_INT(workstations_remove(&stations, index, returned), 0);

    /* A crafting spot needs no materials: it is ready at once. */
    index = workstations_place(&stations, &map, WORKSTATION_CRAFTING_SPOT, 8, 8);
    station = workstation_get_const(&stations, index);
    CHECK(station->built);
    CHECK_EQ_INT(workstation_materials_missing(station), 0);
    CHECK_NEAR(workstation_build_fraction(station), 1.0, 1e-6);
    CHECK(!workstation_add_build_work(&stations, index, needed));
    free_world(&map, &stations, &items);
}

static void test_bills(void)
{
    Map map;
    Workstations stations;
    Items items;
    REQUIRE(init_world(&map, &stations, &items));
    int knapping = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 3, 4);
    int crafting = workstations_place(&stations, &map, WORKSTATION_CRAFTING_SPOT, 8, 8);
    /* Recipes go to their own stations. */
    CHECK_EQ_INT(workstation_add_bill(&stations, crafting, RECIPE_FLAKES), -1);
    CHECK_EQ_INT(workstation_add_bill(&stations, knapping, RECIPE_THROWING_SPEAR), -1);
    CHECK_EQ_INT(workstation_add_bill(&stations, crafting, RECIPE_THROWING_SPEAR), 0);
    CHECK_EQ_INT(workstation_add_bill(&stations, knapping, RECIPE_COUNT), -1);

    int bill = workstation_add_bill(&stations, knapping, RECIPE_FLAKES);
    REQUIRE(bill == 0);
    Workstation *station = workstation_get(&stations, knapping);
    Bill *flakes = &station->bills[bill];
    CHECK_EQ_INT(flakes->mode, BILL_DO_TIMES);
    CHECK_EQ_INT(flakes->target, BILL_TARGET_DEFAULT);
    CHECK_EQ_UINT(flakes->inputs, ITEM_TYPE_BIT(ITEM_FLINT) | ITEM_TYPE_BIT(ITEM_COBBLE));

    CHECK(workstation_set_bill_target(&stations, knapping, bill, 5000));
    CHECK_EQ_INT(flakes->target, BILL_TARGET_MAX);
    CHECK(workstation_set_bill_target(&stations, knapping, bill, 0));
    CHECK_EQ_INT(flakes->target, BILL_TARGET_MIN);
    CHECK(!workstation_set_bill_target(&stations, knapping, 3, 2));
    CHECK(workstation_set_bill_input(&stations, knapping, bill, ITEM_COBBLE, false));
    CHECK_EQ_UINT(flakes->inputs, ITEM_TYPE_BIT(ITEM_FLINT));
    CHECK(!workstation_set_bill_input(&stations, knapping, bill, ITEM_WOOD, true));
    CHECK(workstation_set_bill_mode(&stations, knapping, bill, BILL_UNTIL_STOCK));
    CHECK(!workstation_set_bill_mode(&stations, knapping, bill, BILL_MODE_COUNT));

    /* Modes. */
    Bill test = {.mode = BILL_DO_TIMES, .target = 2, .done = 1};
    CHECK(bill_wants_more(&test, 100));
    test.done = 2;
    CHECK(!bill_wants_more(&test, 0));
    test.mode = BILL_UNTIL_STOCK;
    CHECK(bill_wants_more(&test, 1));
    CHECK(!bill_wants_more(&test, 2));
    test.mode = BILL_FOREVER;
    CHECK(bill_wants_more(&test, 1000));

    for (int i = 1; i < WORKSTATION_BILL_MAX; i++) {
        CHECK_EQ_INT(workstation_add_bill(&stations, knapping, RECIPE_CHOPPER), i);
    }
    CHECK_EQ_INT(workstation_add_bill(&stations, knapping, RECIPE_CHOPPER), -1);
    ItemStack returned;
    CHECK(!workstation_remove_bill(&stations, knapping, 0, &returned));
    CHECK_EQ_INT(station->bill_count, WORKSTATION_BILL_MAX - 1);
    CHECK_EQ_INT(station->bills[0].recipe, RECIPE_CHOPPER);
    CHECK(!workstation_remove_bill(&stations, knapping, WORKSTATION_BILL_MAX, &returned));
    free_world(&map, &stations, &items);
}

static void test_crafting(void)
{
    Map map;
    Workstations stations;
    Items items;
    REQUIRE(init_world(&map, &stations, &items));
    int index = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 3, 4);
    int bill = workstation_add_bill(&stations, index, RECIPE_FLAKES);
    ItemStack flint = item_stack_make(ITEM_FLINT, 1, ITEM_QUALITY_FINE, 0);
    /* Not before the station is built. */
    CHECK(!workstation_start_craft(&stations, index, RECIPE_FLAKES, flint));
    workstation_deliver(&stations, index, item_stack_pristine(ITEM_COBBLE, 1));
    workstation_add_build_work(&stations, index, 1000.0f);

    CHECK(!workstation_start_craft(&stations, index, RECIPE_FLAKES,
                                   item_stack_pristine(ITEM_WOOD, 1)));
    REQUIRE(workstation_start_craft(&stations, index, RECIPE_FLAKES, flint));
    CHECK(!workstation_start_craft(&stations, index, RECIPE_FLAKES, flint)); /* Busy. */

    float needed = recipe_def(RECIPE_FLAKES)->work;
    CHECK(!workstation_add_craft_work(&stations, index, bill, &items, &map, needed * 0.5f, 55.0,
                                      NULL));
    CHECK(workstation_add_craft_work(&stations, index, bill, &items, &map, needed * 0.5f, 55.0,
                                     NULL));
    ItemStack flakes = items_get(&items, 3, 4);
    CHECK_EQ_INT(flakes.type, ITEM_FLAKE);
    CHECK_EQ_INT(flakes.count, 3);
    const Workstation *station = workstation_get_const(&stations, index);
    CHECK_EQ_INT(station->bills[bill].done, 1);
    CHECK_EQ_INT(station->work_input.count, 0);
    CHECK_EQ_INT(station->work_recipe, RECIPE_COUNT);
    CHECK(!workstation_add_craft_work(&stations, index, bill, &items, &map, needed, 55.0, NULL));

    /* The input of an unfinished craft comes back with its bill. */
    REQUIRE(workstation_start_craft(&stations, index, RECIPE_FLAKES, flint));
    ItemStack returned = {0};
    CHECK(workstation_remove_bill(&stations, index, bill, &returned));
    CHECK_EQ_INT(returned.type, ITEM_FLINT);
    CHECK_EQ_INT(workstation_get_const(&stations, index)->work_input.count, 0);
    free_world(&map, &stations, &items);
}

static void test_hash(void)
{
    Map map;
    Workstations stations;
    Items items;
    REQUIRE(init_world(&map, &stations, &items));
    uint64_t empty = workstations_hash(&stations, HASH_FNV1A64_INIT);
    int index = workstations_place(&stations, &map, WORKSTATION_CRAFTING_SPOT, 3, 4);
    uint64_t placed = workstations_hash(&stations, HASH_FNV1A64_INIT);
    CHECK(placed != empty);
    workstation_add_bill(&stations, index, RECIPE_THROWING_SPEAR);
    CHECK(workstations_hash(&stations, HASH_FNV1A64_INIT) != placed);
    free_world(&map, &stations, &items);
}

static void test_bill_inputs_after_tools(void)
{
    Bill bill = {.recipe = RECIPE_CHOPPER, .mode = BILL_DO_TIMES, .target = 1,
                 .inputs = recipe_inputs(RECIPE_CHOPPER)};
    long totals[ITEM_TYPE_COUNT] = {0};
    ItemTypeMask hammer[RECIPE_TOOL_MAX] = {ITEM_TYPE_BIT(ITEM_COBBLE), 0};
    ItemTypeMask none[RECIPE_TOOL_MAX] = {0, 0};
    totals[ITEM_COBBLE] = 1;
    CHECK(bill_has_input(&bill, totals, none));    /* The hammer is already carried. */
    CHECK(!bill_has_input(&bill, totals, hammer)); /* The only cobble is the hammer. */
    totals[ITEM_FLINT] = 1;
    CHECK(bill_has_input(&bill, totals, hammer)); /* Strike the flint with the cobble. */
    bill.inputs = ITEM_TYPE_BIT(ITEM_COBBLE);
    CHECK(!bill_has_input(&bill, totals, hammer)); /* Flint not allowed. */
    totals[ITEM_COBBLE] = 2;
    CHECK(bill_has_input(&bill, totals, hammer));
    bill.inputs = 0;
    CHECK(!bill_has_input(&bill, totals, none));
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_placing_and_removing);
    RUN_TEST(test_building);
    RUN_TEST(test_bills);
    RUN_TEST(test_crafting);
    RUN_TEST(test_bill_inputs_after_tools);
    RUN_TEST(test_hash);
    return test_summary();
}
