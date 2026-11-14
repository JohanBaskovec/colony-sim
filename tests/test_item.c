#include "core/i18n.h"
#include "core/item.h"
#include "core/map.h"
#include "core/util.h"

#include "test_framework.h"

static void test_definitions(void)
{
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        const ItemDef *def = item_def((ItemType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(def->stack_limit > 0 && def->stack_limit <= UINT16_MAX);
    }
    CHECK(item_def(ITEM_TYPE_COUNT) == NULL);
}

static void test_add_and_remove(void)
{
    Items items;
    CHECK(!items_init(&items, 0, 4));
    REQUIRE(items_init(&items, 8, 8));
    int limit = item_def(ITEM_WOOD)->stack_limit;

    CHECK_EQ_INT(items_add(&items, 2, 3, ITEM_WOOD, 10), 10);
    CHECK_EQ_INT(items_add(&items, 2, 3, ITEM_WOOD, limit), limit - 10); /* Up to the limit. */
    CHECK_EQ_INT(items_add(&items, 2, 3, ITEM_WOOD, 1), 0);
    CHECK_EQ_INT(items_add(&items, 2, 3, ITEM_STONE, 1), 0); /* One type per tile. */
    CHECK_EQ_INT(items_add(&items, -1, 3, ITEM_WOOD, 1), 0);
    CHECK_EQ_INT(items_add(&items, 4, 4, ITEM_NONE, 1), 0);
    CHECK_EQ_INT(items_add(&items, 4, 4, ITEM_STONE, 0), 0);
    ItemStack stack = items_get(&items, 2, 3);
    CHECK_EQ_INT(stack.type, ITEM_WOOD);
    CHECK_EQ_INT(stack.count, limit);
    CHECK_EQ_INT(items_get(&items, 99, 0).type, ITEM_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], limit);

    CHECK_EQ_INT(items_remove(&items, 2, 3, 5), 5);
    CHECK_EQ_INT(items_remove(&items, 2, 3, 1000), limit - 5);
    CHECK_EQ_INT(items_get(&items, 2, 3).type, ITEM_NONE); /* Empty tiles forget their type. */
    CHECK_EQ_INT(items_remove(&items, 2, 3, 1), 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 0);
    CHECK_EQ_INT(items_add(&items, 2, 3, ITEM_STONE, 3), 3);
    items_free(&items);
}

static void test_drop_spreads_to_reachable_tiles(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16));
    REQUIRE(items_init(&items, 16, 16));
    int limit = item_def(ITEM_STONE)->stack_limit;

    /* A wall right of x = 8: nothing may land behind it. */
    for (int y = 0; y < 16; y++) {
        map.rock[map_index(&map, 9, y)] = ROCK_GRANITE;
    }
    items_add(&items, 8, 8, ITEM_WOOD, 1); /* Occupied by another type. */
    CHECK_EQ_INT(items_drop(&items, &map, 8, 8, ITEM_STONE, limit * 3), limit * 3);
    CHECK_EQ_INT(items_get(&items, 8, 8).type, ITEM_WOOD);
    for (int y = 0; y < 16; y++) {
        for (int x = 9; x < 16; x++) {
            CHECK_EQ_INT(items_get(&items, x, y).count, 0);
        }
    }
    /* The nearest tiles are filled first. */
    CHECK_EQ_INT(items_get(&items, 8, 7).count, limit);
    CHECK_EQ_INT(items_get(&items, 7, 8).count, limit);
    CHECK_EQ_INT((int)items.totals[ITEM_STONE], limit * 3);

    CHECK_EQ_INT(items_drop(&items, &map, 9, 8, ITEM_STONE, 5), 0); /* Inside the wall. */
    items_free(&items);
    map_free(&map);
}

static void test_drop_without_room_loses_the_rest(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16));
    REQUIRE(items_init(&items, 16, 16));
    /* A single open tile enclosed in rock. */
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        map.rock[i] = ROCK_GRANITE;
    }
    map.rock[map_index(&map, 5, 5)] = ROCK_NONE;
    int limit = item_def(ITEM_WOOD)->stack_limit;
    CHECK_EQ_INT(items_drop(&items, &map, 5, 5, ITEM_WOOD, limit + 7), limit);
    items_free(&items);
    map_free(&map);
}

static void test_hash(void)
{
    Items a, b;
    REQUIRE(items_init(&a, 8, 8));
    REQUIRE(items_init(&b, 8, 8));
    CHECK_EQ_UINT(items_hash(&a, HASH_FNV1A64_INIT), items_hash(&b, HASH_FNV1A64_INIT));
    items_add(&a, 1, 1, ITEM_WOOD, 2);
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_add(&b, 1, 1, ITEM_WOOD, 3);
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_free(&a);
    items_free(&b);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_add_and_remove);
    RUN_TEST(test_drop_spreads_to_reachable_tiles);
    RUN_TEST(test_drop_without_room_loses_the_rest);
    RUN_TEST(test_hash);
    return test_summary();
}
