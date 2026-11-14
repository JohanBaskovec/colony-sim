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

/* The bounding box contains exactly the rows and columns that hold tiles. */
static bool bounds_are_tight(const Stockpiles *s, int index)
{
    const Stockpile *stockpile = stockpile_get(s, index);
    if (stockpile == NULL) {
        return false;
    }
    int min_x = s->width, min_y = s->height, max_x = -1, max_y = -1, cells = 0;
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            if (stockpile_at(s, x, y) == index) {
                min_x = x < min_x ? x : min_x;
                max_x = x > max_x ? x : max_x;
                min_y = y < min_y ? y : min_y;
                max_y = y > max_y ? y : max_y;
                cells++;
            }
        }
    }
    return cells == stockpile->cell_count && stockpile->min_x == min_x &&
           stockpile->max_x == max_x && stockpile->min_y == min_y && stockpile->max_y == max_y;
}

static void test_bounds_follow_the_tiles(void)
{
    Map map;
    Stockpiles s;
    REQUIRE(init_world(&map, &s));
    CHECK_EQ_INT(s.used_count, 0);

    /* The water column (x = 0) is not covered: the box starts at x = 1. */
    int a = stockpile_create(&s, &map, 0, 2, 4, 3);
    REQUIRE(a != STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_get(&s, a)->min_x, 1);
    CHECK_EQ_INT(stockpile_get(&s, a)->max_x, 4);
    CHECK(bounds_are_tight(&s, a));
    CHECK_EQ_INT(s.used_count, 1);

    /* A disjoint extension grows the box; another zone in between is unaffected. */
    int b = stockpile_create(&s, &map, 7, 2, 8, 3);
    REQUIRE(b != STOCKPILE_NONE);
    stockpile_extend(&s, &map, a, 10, 8, 12, 9);
    CHECK(bounds_are_tight(&s, a));
    CHECK(bounds_are_tight(&s, b));
    CHECK_EQ_INT(stockpile_get(&s, a)->max_x, 12);
    CHECK_EQ_INT(stockpile_get(&s, a)->max_y, 9);
    CHECK_EQ_INT(s.used_count, 2);

    /* Removing tiles shrinks it again. */
    stockpiles_remove_rect(&s, 9, 7, 15, 15);
    CHECK(bounds_are_tight(&s, a));
    CHECK_EQ_INT(stockpile_get(&s, a)->max_x, 4);
    stockpiles_remove_rect(&s, 1, 2, 2, 2);
    CHECK(bounds_are_tight(&s, a));

    /* Zones emptied by a removal or deleted no longer count. */
    stockpiles_remove_rect(&s, 0, 0, 5, 5);
    CHECK(stockpile_get(&s, a) == NULL);
    CHECK_EQ_INT(s.used_count, 1);
    stockpile_delete(&s, b);
    CHECK_EQ_INT(s.used_count, 0);
    CHECK_EQ_INT(stockpile_at(&s, 7, 2), STOCKPILE_NONE);
    stockpile_delete(&s, b); /* Twice: no effect. */
    CHECK_EQ_INT(s.used_count, 0);

    /* A reused slot starts from a fresh box. */
    int c = stockpile_create(&s, &map, 13, 13, 14, 14);
    REQUIRE(c != STOCKPILE_NONE);
    CHECK(bounds_are_tight(&s, c));
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
    ItemStack wood = item_stack_pristine(ITEM_WOOD, 1);
    ItemStack berries = item_stack_pristine(ITEM_BERRIES, 1);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, wood), STOCKPILE_PRIORITY_DEFAULT);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 8, 8, wood), 0); /* Not in a stockpile. */
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, item_stack_pristine(ITEM_NONE, 1)), 0);

    stockpile_set_allowed(&s, a, item_types_in_category(ITEM_CATEGORY_FOOD));
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, wood), 0);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, berries), STOCKPILE_PRIORITY_DEFAULT);
    stockpile_set_allowed(&s, a, 0xFFFFFFFFu); /* Unknown bits are dropped. */
    CHECK_EQ_UINT(stockpile_get(&s, a)->allowed, item_types_all());

    stockpile_set_priority(&s, a, 2);
    CHECK_EQ_INT(stockpile_storage_priority(&s, 2, 2, item_stack_pristine(ITEM_STONE_BLOCK, 1)), 2);
    stockpile_set_priority(&s, a, 99);
    CHECK_EQ_INT(stockpile_get(&s, a)->priority, STOCKPILE_PRIORITY_LOWEST);
    stockpile_set_priority(&s, a, -3);
    CHECK_EQ_INT(stockpile_get(&s, a)->priority, STOCKPILE_PRIORITY_HIGHEST);
    free_world(&map, &s);
}

static void test_condition_ranges(void)
{
    Map map;
    Stockpiles s;
    REQUIRE(init_world(&map, &s));
    int a = stockpile_create(&s, &map, 1, 1, 4, 4);
    const Stockpile *pile = stockpile_get(&s, a);
    /* New stockpiles accept any condition. */
    CHECK_EQ_INT(pile->quality.min, ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(pile->quality.max, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(pile->hit_points.min, 0);
    CHECK_EQ_INT(pile->hit_points.max, 100);
    CHECK(stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_CRUDE, 1)));

    uint32_t revision = s.revision;
    stockpile_set_quality_range(&s, a, (IntRange){ITEM_QUALITY_STANDARD, ITEM_QUALITY_FINE});
    CHECK(s.revision != revision);
    revision = s.revision;
    stockpile_set_quality_range(&s, a, (IntRange){ITEM_QUALITY_STANDARD, ITEM_QUALITY_FINE});
    CHECK_EQ_UINT(s.revision, revision); /* No change, no new revision. */

    int wood_hp = item_def(ITEM_WOOD)->max_hit_points;
    CHECK(stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_STANDARD, wood_hp)));
    CHECK(stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_FINE, wood_hp)));
    CHECK(!stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_ROUGH, wood_hp)));
    CHECK(!stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_SUPERB, wood_hp)));
    /* Stone is flawless: above this range. */
    CHECK(!stockpile_accepts(pile, item_stack_pristine(ITEM_STONE_BLOCK, 1)));
    CHECK_EQ_INT(
        stockpile_storage_priority(&s, 2, 2, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_FINE, 9)),
        STOCKPILE_PRIORITY_DEFAULT);
    CHECK_EQ_INT(
        stockpile_storage_priority(&s, 2, 2, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_CRUDE, 9)),
        0);

    /* Hit points in percent of the maximum, rounded down. */
    stockpile_set_quality_range(&s, a, (IntRange){ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST});
    stockpile_set_hit_points_range(&s, a, (IntRange){50, 100});
    CHECK(stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, 0, wood_hp / 2)));
    CHECK(!stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, 0, wood_hp / 2 - 1)));
    CHECK(stockpile_accepts(pile, item_stack_pristine(ITEM_STONE_BLOCK, 1)));
    stockpile_set_hit_points_range(&s, a, (IntRange){0, 99}); /* Damaged items only. */
    CHECK(!stockpile_accepts(pile, item_stack_pristine(ITEM_WOOD, 1)));
    CHECK(stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, 0, wood_hp - 1)));

    /* Bounds are clamped and put in order. */
    stockpile_set_hit_points_range(&s, a, (IntRange){120, -5});
    CHECK_EQ_INT(pile->hit_points.min, 0);
    CHECK_EQ_INT(pile->hit_points.max, 100);
    stockpile_set_quality_range(&s, a, (IntRange){9, -1});
    CHECK_EQ_INT(pile->quality.min, ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(pile->quality.max, ITEM_QUALITY_BEST);
    stockpile_set_quality_range(&s, a, (IntRange){ITEM_QUALITY_FINE, ITEM_QUALITY_ROUGH});
    CHECK_EQ_INT(pile->quality.min, ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(pile->quality.max, ITEM_QUALITY_FINE);

    /* The type filter still applies; empty stacks are never accepted. */
    stockpile_set_allowed(&s, a, ITEM_TYPE_BIT(ITEM_BERRIES));
    CHECK(!stockpile_accepts(pile, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_FINE, wood_hp)));
    CHECK(stockpile_accepts(pile, item_stack_make(ITEM_BERRIES, 1, ITEM_QUALITY_FINE, 10)));
    CHECK(!stockpile_accepts(pile, (ItemStack){.type = ITEM_NONE}));
    CHECK(!stockpile_accepts(NULL, item_stack_make(ITEM_BERRIES, 1, ITEM_QUALITY_FINE, 10)));

    /* Invalid stockpiles are ignored. */
    stockpile_set_quality_range(&s, 40, (IntRange){1, 2});
    stockpile_set_hit_points_range(&s, -1, (IntRange){1, 2});
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
    uint64_t prioritized = stockpiles_hash(&s, HASH_FNV1A64_INIT);
    CHECK(prioritized != created);
    stockpile_set_quality_range(&s, a, (IntRange){ITEM_QUALITY_ROUGH, ITEM_QUALITY_BEST});
    uint64_t quality = stockpiles_hash(&s, HASH_FNV1A64_INIT);
    CHECK(quality != prioritized);
    stockpile_set_hit_points_range(&s, a, (IntRange){0, 90});
    CHECK(stockpiles_hash(&s, HASH_FNV1A64_INIT) != quality);
    free_world(&map, &s);
}

int main(void)
{
    RUN_TEST(test_create_covers_valid_tiles_only);
    RUN_TEST(test_extend_and_remove);
    RUN_TEST(test_bounds_follow_the_tiles);
    RUN_TEST(test_delete);
    RUN_TEST(test_filter_and_priority);
    RUN_TEST(test_condition_ranges);
    RUN_TEST(test_list_full);
    RUN_TEST(test_hash);
    return test_summary();
}
