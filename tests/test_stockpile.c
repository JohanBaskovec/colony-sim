#include "core/stockpile.h"
#include "core/util.h"

#include "test_framework.h"

/* 16 x 16 soil map with water at x = 0 and a rock wall at (5, 5). */
static bool init_world(Map *map, Stockpiles *stockpiles)
{
    if (!map_init(map, 16, 16) || !stockpiles_init(stockpiles, 16, 16)) {
        return false;
    }
    for (int y = 0; y < 16; y++) {
        map->terrain[map_index(map, 0, y)] = TERRAIN_SHALLOW_WATER;
    }
    map->rock[map_index(map, 5, 5)] = ROCK_GRANITE;
    return true;
}

static void free_world(Map *map, Stockpiles *stockpiles)
{
    stockpiles_free(stockpiles);
    map_free(map);
}

static void test_create_covers_valid_tiles_only(void)
{
    Map map;
    Stockpiles s;
    CHECK(!stockpiles_init(&s, 0, 3));
    REQUIRE(init_world(&map, &s));
    CHECK(!stockpile_can_cover(&map, 0, 3)); /* Water. */
    CHECK(!stockpile_can_cover(&map, 5, 5)); /* Rock wall. */
    CHECK(stockpile_can_cover(&map, 3, 3));

    /* Corners in any order: 6 x 6 tiles minus one water column and the rock. */
    int a = stockpile_create(&s, &map, 5, 5, 0, 0);
    REQUIRE(a != STOCKPILE_NONE);
    const Stockpile *stockpile = stockpile_get(&s, a);
    REQUIRE(stockpile != NULL);
    CHECK_EQ_INT(stockpile->cell_count, 5 * 6 - 1);
    CHECK_EQ_INT(stockpile->priority, STOCKPILE_PRIORITY_DEFAULT);
    CHECK_EQ_UINT(stockpile->allowed, item_types_all());
    CHECK_EQ_INT(stockpile_at(&s, 3, 3), a);
    CHECK_EQ_INT(stockpile_at(&s, 0, 3), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_at(&s, 5, 5), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_at(&s, -1, 3), STOCKPILE_NONE);

    /* Overlapping tiles stay in the first stockpile. */
    int b = stockpile_create(&s, &map, 4, 4, 8, 8);
    REQUIRE(b != STOCKPILE_NONE && b != a);
    CHECK_EQ_INT(stockpile_at(&s, 4, 4), a);
    CHECK_EQ_INT(stockpile_at(&s, 8, 8), b);
    CHECK_EQ_INT(stockpile_get(&s, b)->cell_count, 25 - 4);
    CHECK(stockpile_get(&s, b)->number > stockpile_get(&s, a)->number);

    /* Nothing coverable: no stockpile. */
    CHECK_EQ_INT(stockpile_create(&s, &map, 0, 0, 0, 15), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_create(&s, &map, 2, 2, 3, 3), STOCKPILE_NONE); /* All taken. */
    free_world(&map, &s);
}

static void test_extend_and_remove(void)
{
    Map map;
    Stockpiles s;
    REQUIRE(init_world(&map, &s));
    int a = stockpile_create(&s, &map, 1, 1, 2, 2);
    REQUIRE(a != STOCKPILE_NONE);
    uint32_t revision = s.revision;
    CHECK_EQ_INT(stockpile_extend(&s, &map, a, 1, 1, 3, 2), 2); /* Two new tiles. */
    CHECK_EQ_INT(stockpile_get(&s, a)->cell_count, 6);
    CHECK(s.revision != revision);
    CHECK_EQ_INT(stockpile_extend(&s, &map, 42, 1, 1, 3, 3), 0);

    CHECK_EQ_INT(stockpiles_remove_rect(&s, 3, 0, 3, 15), 2);
    CHECK_EQ_INT(stockpile_get(&s, a)->cell_count, 4);
    CHECK_EQ_INT(stockpile_at(&s, 3, 1), STOCKPILE_NONE);

    /* Removing every tile deletes the stockpile. */
    CHECK_EQ_INT(stockpiles_remove_rect(&s, 0, 0, 15, 15), 4);
    CHECK(stockpile_get(&s, a) == NULL);
    free_world(&map, &s);
}

static void test_delete(void)
{
    Map map;
    Stockpiles s;
    REQUIRE(init_world(&map, &s));
    int a = stockpile_create(&s, &map, 1, 1, 4, 4);
    stockpile_delete(&s, a);
    CHECK(stockpile_get(&s, a) == NULL);
    CHECK_EQ_INT(stockpile_at(&s, 2, 2), STOCKPILE_NONE);
    /* The slot is reused but the displayed number is not. */
    int b = stockpile_create(&s, &map, 1, 1, 4, 4);
    CHECK_EQ_INT(b, a);
    CHECK_EQ_INT(stockpile_get(&s, b)->number, 2);
    free_world(&map, &s);
}

static void test_filter_and_priority(void)
{
    Map map;
    Stockpiles s;
    REQUIRE(init_world(&map, &s));
    int a = stockpile_create(&s, &map, 1, 1, 4, 4);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, ITEM_WOOD), STOCKPILE_PRIORITY_DEFAULT);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 8, 8, ITEM_WOOD), 0); /* Not in a stockpile. */
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, ITEM_NONE), 0);

    stockpile_set_allowed(&s, a, item_types_in_category(ITEM_CATEGORY_FOOD));
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, ITEM_WOOD), 0);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, ITEM_BERRIES), STOCKPILE_PRIORITY_DEFAULT);
    stockpile_set_allowed(&s, a, 0xFFFFFFFFu); /* Unknown bits are dropped. */
    CHECK_EQ_UINT(stockpile_get(&s, a)->allowed, item_types_all());

    stockpile_set_priority(&s, a, 2);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, ITEM_STONE), 2);
    stockpile_set_priority(&s, a, 99);
    CHECK_EQ_INT(stockpile_get(&s, a)->priority, STOCKPILE_PRIORITY_LOWEST);
    stockpile_set_priority(&s, a, -3);
    CHECK_EQ_INT(stockpile_get(&s, a)->priority, STOCKPILE_PRIORITY_HIGHEST);
    free_world(&map, &s);
}

static void test_list_full(void)
{
    Map map;
    Stockpiles s;
    REQUIRE(init_world(&map, &s));
    for (int i = 0; i < STOCKPILE_MAX; i++) {
        int x = 1 + i % 15, y = i / 15;
        REQUIRE(stockpile_create(&s, &map, x, y, x, y) != STOCKPILE_NONE);
    }
    CHECK_EQ_INT(stockpile_create(&s, &map, 1, 10, 1, 10), STOCKPILE_NONE);
    free_world(&map, &s);
}

static void test_hash(void)
{
    Map map;
    Stockpiles s;
    REQUIRE(init_world(&map, &s));
    uint64_t empty = stockpiles_hash(&s, HASH_FNV1A64_INIT);
    int a = stockpile_create(&s, &map, 1, 1, 2, 2);
    uint64_t created = stockpiles_hash(&s, HASH_FNV1A64_INIT);
    CHECK(created != empty);
    stockpile_set_priority(&s, a, 1);
    CHECK(stockpiles_hash(&s, HASH_FNV1A64_INIT) != created);
    free_world(&map, &s);
}

int main(void)
{
    RUN_TEST(test_create_covers_valid_tiles_only);
    RUN_TEST(test_extend_and_remove);
    RUN_TEST(test_delete);
    RUN_TEST(test_filter_and_priority);
    RUN_TEST(test_list_full);
    RUN_TEST(test_hash);
    return test_summary();
}
