#include "core/i18n.h"
#include "core/item.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/util.h"

#include "test_framework.h"

static void test_definitions(void)
{
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        const ItemDef *def = item_def((ItemType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(def->weight > 0.0f && def->bulk > 0.0f);
        CHECK(item_stack_limit((ItemType)type) > 0);
    }
    CHECK(item_def(ITEM_TYPE_COUNT) == NULL);
    CHECK(item_is_food(ITEM_BLACKBERRIES));
    CHECK(!item_is_food(ITEM_WOOD));
    CHECK(!item_is_food(ITEM_NONE));
    CHECK(!item_is_food(ITEM_TYPE_COUNT));
}

static void test_stack_limits_follow_bulk(void)
{
    CHECK_EQ_INT(item_stack_limit(ITEM_BLACKBERRIES), 1000);
    CHECK_EQ_INT(item_stack_limit(ITEM_WOOD), 50);
    CHECK_EQ_INT(item_stack_limit(ITEM_STONE_BLOCK), 50);
    CHECK_EQ_INT(item_stack_limit(ITEM_NONE), 0);
    CHECK_EQ_INT(item_stack_limit(ITEM_TYPE_COUNT), 0);
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        const ItemDef *def = item_def((ItemType)type);
        CHECK((float)item_stack_limit((ItemType)type) * def->bulk <= ITEM_TILE_BULK);
    }
    CHECK_NEAR(item_stack_weight(ITEM_WOOD, 50), 50.0 * item_def(ITEM_WOOD)->weight, 1e-3);
    CHECK_NEAR(item_stack_weight(ITEM_NONE, 5), 0.0, 1e-6);

    /* A tile really takes 1000 berries. */
    Items items;
    REQUIRE(items_init(&items, 2, 2));
    CHECK_EQ_INT(items_add(&items, 0, 0, item_stack_pristine(ITEM_BLACKBERRIES, 1200)), 1000);
    items_free(&items);
}

static void test_categories(void)
{
    ItemTypeMask all = item_types_all();
    ItemTypeMask covered = 0;
    for (int category = 0; category < ITEM_CATEGORY_COUNT; category++) {
        CHECK(i18n_is_valid_key(item_category_name_key((ItemCategory)category)));
        ItemTypeMask mask = item_types_in_category((ItemCategory)category);
        CHECK(mask != 0);
        CHECK((covered & mask) == 0); /* Each type belongs to one category. */
        covered |= mask;
    }
    CHECK_EQ_UINT(covered, all);
    CHECK(!(all & ITEM_TYPE_BIT(ITEM_NONE)));
    CHECK(item_types_in_category(ITEM_CATEGORY_FOOD) & ITEM_TYPE_BIT(ITEM_BLACKBERRIES));
    CHECK(item_types_in_category(ITEM_CATEGORY_RESOURCES) & ITEM_TYPE_BIT(ITEM_WOOD));
    CHECK(item_types_in_category(ITEM_CATEGORY_RESOURCES) & ITEM_TYPE_BIT(ITEM_STONE_BLOCK));
    CHECK(item_category_name_key(ITEM_CATEGORY_COUNT) == NULL);
    CHECK_EQ_UINT(item_types_in_category(ITEM_CATEGORY_COUNT), 0);
}

static void test_additions_counter(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4));
    CHECK_EQ_UINT(items.additions, 0);
    items_add(&items, 1, 1, item_stack_pristine(ITEM_WOOD, 5));
    CHECK_EQ_UINT(items.additions, 1);
    items_remove(&items, 1, 1, 5);
    CHECK_EQ_UINT(items.additions, 1); /* Removals do not count. */
    items_free(&items);
}

static void test_add_and_remove(void)
{
    Items items;
    CHECK(!items_init(&items, 0, 4));
    REQUIRE(items_init(&items, 8, 8));
    int limit = item_stack_limit(ITEM_WOOD);

    CHECK_EQ_INT(items_add(&items, 2, 3, item_stack_pristine(ITEM_WOOD, 10)), 10);
    /* Up to the limit. */
    CHECK_EQ_INT(items_add(&items, 2, 3, item_stack_pristine(ITEM_WOOD, limit)), limit - 10);
    CHECK_EQ_INT(items_add(&items, 2, 3, item_stack_pristine(ITEM_WOOD, 1)), 0);
    /* One type per tile. */
    CHECK_EQ_INT(items_add(&items, 2, 3, item_stack_pristine(ITEM_STONE_BLOCK, 1)), 0);
    CHECK_EQ_INT(items_add(&items, -1, 3, item_stack_pristine(ITEM_WOOD, 1)), 0);
    CHECK_EQ_INT(items_add(&items, 4, 4, item_stack_pristine(ITEM_NONE, 1)), 0);
    CHECK_EQ_INT(items_add(&items, 4, 4, item_stack_pristine(ITEM_STONE_BLOCK, 0)), 0);
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
    CHECK_EQ_INT(items_add(&items, 2, 3, item_stack_pristine(ITEM_STONE_BLOCK, 3)), 3);
    items_free(&items);
}

static void test_drop_spreads_to_reachable_tiles(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16));
    REQUIRE(items_init(&items, 16, 16));
    int limit = item_stack_limit(ITEM_STONE_BLOCK);

    /* A wall right of x = 8: nothing may land behind it. */
    for (int y = 0; y < 16; y++) {
        map.rock[map_index(&map, 9, y)] = ROCK_GRANITE;
    }
    items_add(&items, 8, 8, item_stack_pristine(ITEM_WOOD, 1)); /* Occupied by another type. */
    CHECK_EQ_INT(items_drop(&items, &map, 8, 8, item_stack_pristine(ITEM_STONE_BLOCK, limit * 3)),
                 limit * 3);
    CHECK_EQ_INT(items_get(&items, 8, 8).type, ITEM_WOOD);
    for (int y = 0; y < 16; y++) {
        for (int x = 9; x < 16; x++) {
            CHECK_EQ_INT(items_get(&items, x, y).count, 0);
        }
    }
    /* The nearest tiles are filled first. */
    CHECK_EQ_INT(items_get(&items, 8, 7).count, limit);
    CHECK_EQ_INT(items_get(&items, 7, 8).count, limit);
    CHECK_EQ_INT((int)items.totals[ITEM_STONE_BLOCK], limit * 3);

    /* Inside the wall. */
    CHECK_EQ_INT(items_drop(&items, &map, 9, 8, item_stack_pristine(ITEM_STONE_BLOCK, 5)), 0);
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
    int limit = item_stack_limit(ITEM_WOOD);
    CHECK_EQ_INT(items_drop(&items, &map, 5, 5, item_stack_pristine(ITEM_WOOD, limit + 7)), limit);
    items_free(&items);
    map_free(&map);
}

static void test_condition_definitions(void)
{
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        CHECK(item_def((ItemType)type)->max_hit_points > 0);
    }
    /* Stone is always perfect; wood and berries vary and can be damaged. */
    CHECK(!item_def(ITEM_STONE_BLOCK)->has_quality);
    CHECK(!item_def(ITEM_STONE_BLOCK)->damageable);
    CHECK(item_def(ITEM_WOOD)->has_quality);
    CHECK(item_def(ITEM_WOOD)->damageable);
    CHECK(item_def(ITEM_BLACKBERRIES)->has_quality);
    CHECK(item_def(ITEM_BLACKBERRIES)->damageable);
}

static void test_quality_grades(void)
{
    for (int quality = 0; quality < ITEM_QUALITY_COUNT; quality++) {
        CHECK(i18n_is_valid_key(item_quality_name_key((ItemQuality)quality)));
    }
    CHECK(item_quality_name_key(ITEM_QUALITY_COUNT) == NULL);
}

static void test_stack_make_keeps_condition_valid(void)
{
    int wood_hp = item_def(ITEM_WOOD)->max_hit_points;
    ItemStack wood = item_stack_make(ITEM_WOOD, 5, ITEM_QUALITY_ROUGH, 20);
    CHECK_EQ_INT(wood.type, ITEM_WOOD);
    CHECK_EQ_INT(wood.count, 5);
    CHECK_EQ_INT(wood.quality, ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(wood.hit_points, 20);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 99, 999).quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, -4, 999).quality, ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 2, 999).hit_points, wood_hp);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 2, 0).hit_points, 1); /* Never destroyed here. */

    /* Stone ignores what it is given. */
    ItemStack stone = item_stack_make(ITEM_STONE_BLOCK, 2, ITEM_QUALITY_CRUDE, 3);
    CHECK_EQ_INT(stone.quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(stone.hit_points, item_def(ITEM_STONE_BLOCK)->max_hit_points);

    CHECK_EQ_INT(item_stack_make(ITEM_NONE, 2, 2, 5).type, ITEM_NONE);
    CHECK_EQ_INT(item_stack_make(ITEM_TYPE_COUNT, 2, 2, 5).type, ITEM_NONE);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 0, 2, 5).type, ITEM_NONE);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 0, 2, 5).count, 0);

    ItemStack pristine = item_stack_pristine(ITEM_BLACKBERRIES, 7);
    CHECK_EQ_INT(pristine.quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(pristine.hit_points, item_def(ITEM_BLACKBERRIES)->max_hit_points);
    CHECK_EQ_INT(item_stack_hit_points_percent(pristine), 100);

    CHECK(item_stacks_match(wood, item_stack_make(ITEM_WOOD, 9, ITEM_QUALITY_ROUGH, 60)));
    CHECK(!item_stacks_match(wood, item_stack_make(ITEM_WOOD, 5, ITEM_QUALITY_FINE, 20)));
    CHECK(!item_stacks_match(wood, item_stack_make(ITEM_BLACKBERRIES, 5, ITEM_QUALITY_ROUGH, 5)));
}

static void test_hit_points_percent(void)
{
    int max = item_def(ITEM_WOOD)->max_hit_points; /* 60 */
    CHECK_EQ_INT(item_hit_points_at_percent(ITEM_WOOD, 100), max);
    CHECK_EQ_INT(item_hit_points_at_percent(ITEM_WOOD, 50), max / 2);
    CHECK_EQ_INT(item_hit_points_at_percent(ITEM_WOOD, 0), 1);
    CHECK_EQ_INT(item_hit_points_at_percent(ITEM_WOOD, 250), max);
    CHECK_EQ_INT(item_hit_points_at_percent(ITEM_NONE, 50), 0);
    /* Rounded down: 100 % only when undamaged. */
    CHECK_EQ_INT(item_stack_hit_points_percent(item_stack_make(ITEM_WOOD, 1, 2, max - 1)), 98);
    CHECK_EQ_INT(item_stack_hit_points_percent(item_stack_make(ITEM_WOOD, 1, 2, 30)), 50);
    CHECK_EQ_INT(item_stack_hit_points_percent((ItemStack){.type = ITEM_NONE}), 0);
}

static void test_only_same_grade_stacks(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4));
    CHECK_EQ_INT(items_add(&items, 1, 1, item_stack_make(ITEM_WOOD, 30, ITEM_QUALITY_FINE, 30)),
                 30);
    /* Another grade does not fit on the tile. */
    CHECK_EQ_INT(items_add(&items, 1, 1, item_stack_make(ITEM_WOOD, 5, ITEM_QUALITY_ROUGH, 30)),
                 0);
    /* The same grade merges; hit points are averaged by count. */
    CHECK_EQ_INT(items_add(&items, 1, 1, item_stack_make(ITEM_WOOD, 10, ITEM_QUALITY_FINE, 60)),
                 10);
    ItemStack stack = items_get(&items, 1, 1);
    CHECK_EQ_INT(stack.count, 40);
    CHECK_EQ_INT(stack.quality, ITEM_QUALITY_FINE);
    CHECK_EQ_INT(stack.hit_points, 38); /* (30 × 30 + 10 × 60) / 40 = 37.5, rounded */

    /* Only what fits is merged, and only that counts in the average. */
    int room = item_stack_limit(ITEM_WOOD) - 40;
    CHECK_EQ_INT(items_add(&items, 1, 1, item_stack_make(ITEM_WOOD, 100, ITEM_QUALITY_FINE, 1)),
                 room);
    CHECK_EQ_INT(items_get(&items, 1, 1).hit_points, (40 * 38 + room * 1 + 25) / 50);

    /* Removed items leave the rest unchanged; an emptied tile forgets it all. */
    ItemStack before = items_get(&items, 1, 1);
    items_remove(&items, 1, 1, 5);
    CHECK_EQ_INT(items_get(&items, 1, 1).quality, before.quality);
    CHECK_EQ_INT(items_get(&items, 1, 1).hit_points, before.hit_points);
    items_remove(&items, 1, 1, 1000);
    ItemStack empty = items_get(&items, 1, 1);
    CHECK_EQ_INT(empty.quality, 0);
    CHECK_EQ_INT(empty.hit_points, 0);
    CHECK_EQ_INT(items_add(&items, 1, 1, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_CRUDE, 9)), 1);
    CHECK_EQ_INT(items_get(&items, 1, 1).quality, ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(items_get(&items, 1, 1).hit_points, 9);

    /* A hand-built stack is made valid when added. */
    ItemStack bogus = {.type = ITEM_STONE_BLOCK, .quality = 0, .count = 2, .hit_points = 1};
    CHECK_EQ_INT(items_add(&items, 2, 2, bogus), 2);
    CHECK_EQ_INT(items_get(&items, 2, 2).quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(items_get(&items, 2, 2).hit_points, item_def(ITEM_STONE_BLOCK)->max_hit_points);
    items_free(&items);
}

static void test_stack_merge(void)
{
    ItemStack hand = {.type = ITEM_NONE};
    CHECK(item_stack_merge(&hand, item_stack_make(ITEM_WOOD, 3, ITEM_QUALITY_FINE, 60)));
    CHECK_EQ_INT(hand.count, 3);
    CHECK_EQ_INT(hand.hit_points, 60);
    CHECK(item_stack_merge(&hand, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_FINE, 20)));
    CHECK_EQ_INT(hand.count, 4);
    CHECK_EQ_INT(hand.hit_points, 50); /* (3 × 60 + 20) / 4 */
    CHECK_EQ_INT(hand.quality, ITEM_QUALITY_FINE);
    /* Another grade or type: refused, nothing changes. */
    CHECK(!item_stack_merge(&hand, item_stack_make(ITEM_WOOD, 2, ITEM_QUALITY_ROUGH, 60)));
    CHECK(!item_stack_merge(&hand, item_stack_pristine(ITEM_STONE_BLOCK, 2)));
    CHECK_EQ_INT(hand.count, 4);
    /* Merging nothing is fine. */
    CHECK(item_stack_merge(&hand, (ItemStack){.type = ITEM_NONE}));
    CHECK_EQ_INT(hand.count, 4);
    /* Counts are capped. */
    ItemStack big = item_stack_make(ITEM_BLACKBERRIES, UINT16_MAX - 1, ITEM_QUALITY_FINE, 10);
    CHECK(item_stack_merge(&big, item_stack_make(ITEM_BLACKBERRIES, 5, ITEM_QUALITY_FINE, 10)));
    CHECK_EQ_INT(big.count, UINT16_MAX);
}

static void test_merged_hit_points_stay_between(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4));
    Rng rng;
    rng_seed(&rng, 99, 0);
    for (int i = 0; i < 500; i++) {
        ItemStack a = item_stack_make(ITEM_BLACKBERRIES, rng_range_i32(&rng, 1, 400), 2,
                                      rng_range_i32(&rng, 1, 10));
        ItemStack b = item_stack_make(ITEM_BLACKBERRIES, rng_range_i32(&rng, 1, 400), 2,
                                      rng_range_i32(&rng, 1, 10));
        items_remove(&items, 0, 0, 1000);
        items_add(&items, 0, 0, a);
        items_add(&items, 0, 0, b);
        ItemStack merged = items_get(&items, 0, 0);
        int lo = a.hit_points < b.hit_points ? a.hit_points : b.hit_points;
        int hi = a.hit_points < b.hit_points ? b.hit_points : a.hit_points;
        CHECK(merged.hit_points >= lo && merged.hit_points <= hi);
    }
    items_free(&items);
}

static void test_drop_skips_other_grades(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16));
    REQUIRE(items_init(&items, 16, 16));
    items_add(&items, 8, 8, item_stack_make(ITEM_WOOD, 1, ITEM_QUALITY_SUPERB, 60));
    ItemStack crude = item_stack_make(ITEM_WOOD, 4, ITEM_QUALITY_CRUDE, 60);
    CHECK_EQ_INT(items_drop(&items, &map, 8, 8, crude), 4);
    CHECK_EQ_INT(items_get(&items, 8, 8).count, 1);
    CHECK_EQ_INT(items_get(&items, 8, 7).count, 4); /* Next tile. */
    CHECK_EQ_INT(items_get(&items, 8, 7).quality, ITEM_QUALITY_CRUDE);
    items_free(&items);
    map_free(&map);
}

static void test_drop_keeps_condition(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16));
    REQUIRE(items_init(&items, 16, 16));
    int limit = item_stack_limit(ITEM_WOOD);
    ItemStack fine = item_stack_make(ITEM_WOOD, limit + 5, ITEM_QUALITY_FINE, 21);
    CHECK_EQ_INT(items_drop(&items, &map, 8, 8, fine), limit + 5);
    CHECK_EQ_INT(items_get(&items, 8, 8).quality, ITEM_QUALITY_FINE);
    CHECK_EQ_INT(items_get(&items, 8, 8).hit_points, 21);
    CHECK_EQ_INT(items_get(&items, 8, 7).count, 5); /* The rest, same condition. */
    CHECK_EQ_INT(items_get(&items, 8, 7).quality, ITEM_QUALITY_FINE);
    CHECK_EQ_INT(items_get(&items, 8, 7).hit_points, 21);
    items_free(&items);
    map_free(&map);
}

static void test_hash(void)
{
    Items a, b;
    REQUIRE(items_init(&a, 8, 8));
    REQUIRE(items_init(&b, 8, 8));
    CHECK_EQ_UINT(items_hash(&a, HASH_FNV1A64_INIT), items_hash(&b, HASH_FNV1A64_INIT));
    items_add(&a, 1, 1, item_stack_pristine(ITEM_WOOD, 2));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_add(&b, 1, 1, item_stack_pristine(ITEM_WOOD, 3));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));

    /* Same items in another condition. */
    items_remove(&b, 1, 1, 1);
    CHECK_EQ_UINT(items_hash(&a, HASH_FNV1A64_INIT), items_hash(&b, HASH_FNV1A64_INIT));
    items_remove(&b, 1, 1, 2);
    items_add(&b, 1, 1, item_stack_make(ITEM_WOOD, 2, ITEM_QUALITY_FINE, 60));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_remove(&b, 1, 1, 2);
    items_add(&b, 1, 1, item_stack_make(ITEM_WOOD, 2, ITEM_QUALITY_BEST, 59));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_free(&a);
    items_free(&b);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_stack_limits_follow_bulk);
    RUN_TEST(test_categories);
    RUN_TEST(test_additions_counter);
    RUN_TEST(test_add_and_remove);
    RUN_TEST(test_condition_definitions);
    RUN_TEST(test_quality_grades);
    RUN_TEST(test_stack_make_keeps_condition_valid);
    RUN_TEST(test_hit_points_percent);
    RUN_TEST(test_only_same_grade_stacks);
    RUN_TEST(test_stack_merge);
    RUN_TEST(test_merged_hit_points_stay_between);
    RUN_TEST(test_drop_keeps_condition);
    RUN_TEST(test_drop_skips_other_grades);
    RUN_TEST(test_drop_spreads_to_reachable_tiles);
    RUN_TEST(test_drop_without_room_loses_the_rest);
    RUN_TEST(test_hash);
    return test_summary();
}
