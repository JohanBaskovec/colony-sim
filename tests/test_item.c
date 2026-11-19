#include "core/i18n.h"
#include "core/item.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/util.h"

#include "test_framework.h"

#include <math.h>

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
    REQUIRE(items_init(&items, 2, 2, 1));
    CHECK_EQ_INT(items_add(&items, 0, 0, 0, item_stack_pristine(ITEM_BLACKBERRIES, 1200)), 1000);
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
    REQUIRE(items_init(&items, 4, 4, 1));
    CHECK_EQ_UINT(items.additions, 0);
    items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_WOOD, 5));
    CHECK_EQ_UINT(items.additions, 1);
    items_remove(&items, 1, 1, 0, 5);
    CHECK_EQ_UINT(items.additions, 1); /* Removals do not count. */
    items_free(&items);
}

static void test_add_and_remove(void)
{
    Items items;
    CHECK(!items_init(&items, 0, 4, 1));
    REQUIRE(items_init(&items, 8, 8, 1));
    int limit = item_stack_limit(ITEM_WOOD);

    CHECK_EQ_INT(items_add(&items, 2, 3, 0, item_stack_pristine(ITEM_WOOD, 10)), 10);
    /* Up to the limit. */
    CHECK_EQ_INT(items_add(&items, 2, 3, 0, item_stack_pristine(ITEM_WOOD, limit)), limit - 10);
    CHECK_EQ_INT(items_add(&items, 2, 3, 0, item_stack_pristine(ITEM_WOOD, 1)), 0);
    /* One type per tile. */
    CHECK_EQ_INT(items_add(&items, 2, 3, 0, item_stack_pristine(ITEM_STONE_BLOCK, 1)), 0);
    CHECK_EQ_INT(items_add(&items, -1, 3, 0, item_stack_pristine(ITEM_WOOD, 1)), 0);
    CHECK_EQ_INT(items_add(&items, 4, 4, 0, item_stack_pristine(ITEM_NONE, 1)), 0);
    CHECK_EQ_INT(items_add(&items, 4, 4, 0, item_stack_pristine(ITEM_STONE_BLOCK, 0)), 0);
    ItemStack stack = items_get(&items, 2, 3, 0);
    CHECK_EQ_INT(stack.type, ITEM_WOOD);
    CHECK_EQ_INT(stack.count, limit);
    CHECK_EQ_INT(items_get(&items, 99, 0, 0).type, ITEM_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], limit);

    CHECK_EQ_INT(items_remove(&items, 2, 3, 0, 5), 5);
    CHECK_EQ_INT(items_remove(&items, 2, 3, 0, 1000), limit - 5);
    CHECK_EQ_INT(items_get(&items, 2, 3, 0).type, ITEM_NONE); /* Empty tiles forget their type. */
    CHECK_EQ_INT(items_remove(&items, 2, 3, 0, 1), 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 0);
    CHECK_EQ_INT(items_add(&items, 2, 3, 0, item_stack_pristine(ITEM_STONE_BLOCK, 3)), 3);
    items_free(&items);
}

static void test_drop_spreads_to_reachable_tiles(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16, 1));
    REQUIRE(items_init(&items, 16, 16, 1));
    int limit = item_stack_limit(ITEM_STONE_BLOCK);

    /* A wall right of x = 8: nothing may land behind it. */
    for (int y = 0; y < 16; y++) {
        map.rock[map_index(&map, 9, y, 0)] = ROCK_GRANITE;
    }
    items_add(&items, 8, 8, 0, item_stack_pristine(ITEM_WOOD, 1)); /* Occupied by another type. */
    CHECK_EQ_INT(
        items_drop(&items, &map, 8, 8, 0, item_stack_pristine(ITEM_STONE_BLOCK, limit * 3)),
        limit * 3);
    CHECK_EQ_INT(items_get(&items, 8, 8, 0).type, ITEM_WOOD);
    for (int y = 0; y < 16; y++) {
        for (int x = 9; x < 16; x++) {
            CHECK_EQ_INT(items_get(&items, x, y, 0).count, 0);
        }
    }
    /* The nearest tiles are filled first. */
    CHECK_EQ_INT(items_get(&items, 8, 7, 0).count, limit);
    CHECK_EQ_INT(items_get(&items, 7, 8, 0).count, limit);
    CHECK_EQ_INT((int)items.totals[ITEM_STONE_BLOCK], limit * 3);

    /* Inside the wall. */
    CHECK_EQ_INT(items_drop(&items, &map, 9, 8, 0, item_stack_pristine(ITEM_STONE_BLOCK, 5)), 0);
    items_free(&items);
    map_free(&map);
}

static void test_drop_without_room_loses_the_rest(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16, 1));
    REQUIRE(items_init(&items, 16, 16, 1));
    /* A single open tile enclosed in rock. */
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        map.rock[i] = ROCK_GRANITE;
    }
    map.rock[map_index(&map, 5, 5, 0)] = ROCK_NONE;
    int limit = item_stack_limit(ITEM_WOOD);
    CHECK_EQ_INT(items_drop(&items, &map, 5, 5, 0, item_stack_pristine(ITEM_WOOD, limit + 7)),
                 limit);
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

static void test_quality_is_a_value_from_0_to_100(void)
{
    CHECK_EQ_INT(ITEM_QUALITY_WORST, 0);
    CHECK_EQ_INT(ITEM_QUALITY_BEST, 100);
    CHECK(ITEM_QUALITY_WORST < ITEM_QUALITY_STANDARD && ITEM_QUALITY_STANDARD < ITEM_QUALITY_BEST);
    /* Any value in between is kept, with the clamping at both ends. */
    for (int quality = 0; quality <= 100; quality++) {
        CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, quality, 10).quality, quality);
    }
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 101, 10).quality, 100);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 5000, 10).quality, 100);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, -1, 10).quality, 0);
}

static void test_stack_make_keeps_condition_valid(void)
{
    int wood_hp = item_def(ITEM_WOOD)->max_hit_points;
    ItemStack wood = item_stack_make(ITEM_WOOD, 5, TEST_QUALITY_ROUGH, 20);
    CHECK_EQ_INT(wood.type, ITEM_WOOD);
    CHECK_EQ_INT(wood.count, 5);
    CHECK_EQ_INT(wood.quality, TEST_QUALITY_ROUGH);
    CHECK_EQ_INT(wood.hit_points, 20);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 199, 999).quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, -4, 999).quality, ITEM_QUALITY_WORST);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 2, 999).hit_points, wood_hp);
    CHECK_EQ_INT(item_stack_make(ITEM_WOOD, 1, 2, 0).hit_points, 1); /* Never destroyed here. */

    /* Stone ignores what it is given. */
    ItemStack stone = item_stack_make(ITEM_STONE_BLOCK, 2, TEST_QUALITY_CRUDE, 3);
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

    CHECK(item_stacks_match(wood, item_stack_make(ITEM_WOOD, 9, TEST_QUALITY_ROUGH, 60)));
    CHECK(item_stacks_match(
        wood, item_stack_make(ITEM_WOOD, 5, TEST_QUALITY_FINE, 20))); /* Any quality. */
    CHECK(!item_stacks_match(wood, item_stack_make(ITEM_BLACKBERRIES, 5, TEST_QUALITY_ROUGH, 5)));
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

static void test_same_type_stacks_whatever_the_quality(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    CHECK_EQ_INT(items_add(&items, 1, 1, 0, item_stack_make(ITEM_WOOD, 30, 70, 30)), 30);
    /* Another quality merges too: quality and hit points are averaged by count. */
    CHECK_EQ_INT(items_add(&items, 1, 1, 0, item_stack_make(ITEM_WOOD, 10, 30, 60)), 10);
    ItemStack stack = items_get(&items, 1, 1, 0);
    CHECK_EQ_INT(stack.count, 40);
    CHECK_EQ_INT(stack.quality, 60);    /* (30 × 70 + 10 × 30) / 40 */
    CHECK_EQ_INT(stack.hit_points, 38); /* (30 × 30 + 10 × 60) / 40 = 37.5, rounded */
    /* Another type does not fit on the tile. */
    CHECK_EQ_INT(items_add(&items, 1, 1, 0, item_stack_make(ITEM_BLACKBERRIES, 5, 70, 5)), 0);

    /* Only what fits is added; the summary is the average of every item there. */
    int room = item_stack_limit(ITEM_WOOD) - 40;
    REQUIRE(room == 10);
    CHECK_EQ_INT(items_add(&items, 1, 1, 0, item_stack_make(ITEM_WOOD, 100, 10, 1)), room);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).hit_points, (30 * 30 + 10 * 60 + 10 * 1 + 25) / 50);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).quality, (30 * 70 + 10 * 30 + 10 * 10 + 25) / 50);

    /* Every item keeps its own condition: the newest are taken first, and the
     * rest keep theirs. */
    ItemStack taken = items_take(&items, 1, 1, 0, 5);
    CHECK_EQ_INT(taken.count, 5);
    CHECK_EQ_INT(taken.quality, 10);
    CHECK_EQ_INT(taken.hit_points, 1);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).quality, (30 * 70 + 10 * 30 + 5 * 10 + 22) / 45);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).hit_points, (30 * 30 + 10 * 60 + 5 * 1 + 22) / 45);
    taken = items_take(&items, 1, 1, 0, 15);
    CHECK_EQ_INT(taken.count, 15);
    CHECK_EQ_INT(taken.quality, (5 * 10 + 10 * 30 + 7) / 15);
    int seen = 0;
    for (const Item *item = items_first(&items, 1 * 4 + 1); item != NULL;
         item = items_next(&items, item)) {
        CHECK_EQ_INT(item->quality, 70);
        CHECK_EQ_INT(item->hit_points, 30);
        seen++;
    }
    CHECK_EQ_INT(seen, 30);
    CHECK_EQ_INT(items_count_at(&items, 1, 1, 0, ITEM_WOOD), 30);
    CHECK_EQ_INT(items_count_at(&items, 1, 1, 0, ITEM_NONE), 30);
    CHECK_EQ_INT(items_count_at(&items, 1, 1, 0, ITEM_BLACKBERRIES), 0);
    items_remove(&items, 1, 1, 0, 1000);
    ItemStack empty = items_get(&items, 1, 1, 0);
    CHECK_EQ_INT(empty.quality, 0);
    CHECK_EQ_INT(empty.hit_points, 0);
    CHECK_EQ_INT(items_add(&items, 1, 1, 0, item_stack_make(ITEM_WOOD, 1, TEST_QUALITY_CRUDE, 9)),
                 1);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).quality, TEST_QUALITY_CRUDE);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).hit_points, 9);

    /* A hand-built stack is made valid when added. */
    ItemStack bogus = {.type = ITEM_STONE_BLOCK, .quality = 0, .count = 2, .hit_points = 1};
    CHECK_EQ_INT(items_add(&items, 2, 2, 0, bogus), 2);
    CHECK_EQ_INT(items_get(&items, 2, 2, 0).quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(items_get(&items, 2, 2, 0).hit_points, item_def(ITEM_STONE_BLOCK)->max_hit_points);
    items_free(&items);
}

static void test_stack_merge(void)
{
    ItemStack hand = {.type = ITEM_NONE};
    CHECK(item_stack_merge(&hand, item_stack_make(ITEM_WOOD, 3, TEST_QUALITY_FINE, 60)));
    CHECK_EQ_INT(hand.count, 3);
    CHECK_EQ_INT(hand.hit_points, 60);
    CHECK(item_stack_merge(&hand, item_stack_make(ITEM_WOOD, 1, TEST_QUALITY_FINE, 20)));
    CHECK_EQ_INT(hand.count, 4);
    CHECK_EQ_INT(hand.hit_points, 50); /* (3 × 60 + 20) / 4 */
    CHECK_EQ_INT(hand.quality, TEST_QUALITY_FINE);
    /* Another type: refused, nothing changes. Another quality: averaged. */
    CHECK(!item_stack_merge(&hand, item_stack_pristine(ITEM_STONE_BLOCK, 2)));
    CHECK_EQ_INT(hand.count, 4);
    CHECK(item_stack_merge(&hand, item_stack_make(ITEM_WOOD, 4, TEST_QUALITY_CRUDE, 50)));
    CHECK_EQ_INT(hand.count, 8);
    CHECK_EQ_INT(hand.quality, (TEST_QUALITY_FINE + TEST_QUALITY_CRUDE) / 2);
    hand = item_stack_make(ITEM_WOOD, 4, TEST_QUALITY_FINE, 50);
    /* Merging nothing is fine. */
    CHECK(item_stack_merge(&hand, (ItemStack){.type = ITEM_NONE}));
    CHECK_EQ_INT(hand.count, 4);
    /* Counts are capped. */
    ItemStack big = item_stack_make(ITEM_BLACKBERRIES, UINT16_MAX - 1, TEST_QUALITY_FINE, 10);
    CHECK(item_stack_merge(&big, item_stack_make(ITEM_BLACKBERRIES, 5, TEST_QUALITY_FINE, 10)));
    CHECK_EQ_INT(big.count, UINT16_MAX);
}

static void test_merged_hit_points_stay_between(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    Rng rng;
    rng_seed(&rng, 99, 0);
    for (int i = 0; i < 500; i++) {
        ItemStack a = item_stack_make(ITEM_BLACKBERRIES, rng_range_i32(&rng, 1, 400), 2,
                                      rng_range_i32(&rng, 1, 10));
        ItemStack b = item_stack_make(ITEM_BLACKBERRIES, rng_range_i32(&rng, 1, 400), 2,
                                      rng_range_i32(&rng, 1, 10));
        items_remove(&items, 0, 0, 0, 1000);
        items_add(&items, 0, 0, 0, a);
        items_add(&items, 0, 0, 0, b);
        ItemStack merged = items_get(&items, 0, 0, 0);
        int lo = a.hit_points < b.hit_points ? a.hit_points : b.hit_points;
        int hi = a.hit_points < b.hit_points ? b.hit_points : a.hit_points;
        CHECK(merged.hit_points >= lo && merged.hit_points <= hi);
    }
    items_free(&items);
}

static void test_drop_skips_other_types(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16, 1));
    REQUIRE(items_init(&items, 16, 16, 1));
    items_add(&items, 8, 8, 0, item_stack_make(ITEM_WOOD, 1, TEST_QUALITY_SUPERB, 60));
    ItemStack branches = item_stack_make(ITEM_BRANCHES, 4, TEST_QUALITY_CRUDE, 10);
    CHECK_EQ_INT(items_drop(&items, &map, 8, 8, 0, branches), 4);
    CHECK_EQ_INT(items_get(&items, 8, 8, 0).count, 1);
    CHECK_EQ_INT(items_get(&items, 8, 7, 0).count, 4); /* Next tile. */
    CHECK_EQ_INT(items_get(&items, 8, 7, 0).quality, TEST_QUALITY_CRUDE);
    /* The same type, of another quality, joins the stack. */
    ItemStack more = item_stack_make(ITEM_WOOD, 3, TEST_QUALITY_CRUDE, 60);
    CHECK_EQ_INT(items_drop(&items, &map, 8, 8, 0, more), 3);
    CHECK_EQ_INT(items_get(&items, 8, 8, 0).count, 4);
    items_free(&items);
    map_free(&map);
}

static void test_drop_keeps_condition(void)
{
    Map map;
    Items items;
    REQUIRE(map_init(&map, 16, 16, 1));
    REQUIRE(items_init(&items, 16, 16, 1));
    int limit = item_stack_limit(ITEM_WOOD);
    ItemStack fine = item_stack_make(ITEM_WOOD, limit + 5, TEST_QUALITY_FINE, 21);
    CHECK_EQ_INT(items_drop(&items, &map, 8, 8, 0, fine), limit + 5);
    CHECK_EQ_INT(items_get(&items, 8, 8, 0).quality, TEST_QUALITY_FINE);
    CHECK_EQ_INT(items_get(&items, 8, 8, 0).hit_points, 21);
    CHECK_EQ_INT(items_get(&items, 8, 7, 0).count, 5); /* The rest, same condition. */
    CHECK_EQ_INT(items_get(&items, 8, 7, 0).quality, TEST_QUALITY_FINE);
    CHECK_EQ_INT(items_get(&items, 8, 7, 0).hit_points, 21);
    items_free(&items);
    map_free(&map);
}

static void test_rot_definitions(void)
{
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        const ItemDef *def = item_def((ItemType)type);
        if (item_rots((ItemType)type)) {
            /* Food, or a body: not food. */
            CHECK(item_is_food((ItemType)type) != (def->category == ITEM_CATEGORY_BODIES));
            CHECK(def->damageable);
            CHECK_EQ_INT(def->transforms_into, ITEM_NONE);
        }
    }
    /* Every food rots; nuts and roots keep longer than fruit, greens and
     * mushrooms. */
    CHECK(item_rots(ITEM_MUSHROOMS) && item_rots(ITEM_ROASTED_RHIZOMES));
    CHECK(!item_rots(ITEM_WOOD) && !item_rots(ITEM_NONE) && !item_rots(ITEM_TYPE_COUNT));
    CHECK(item_def(ITEM_HAZELNUTS)->rot_days > item_def(ITEM_CATTAIL_RHIZOMES)->rot_days);
    CHECK(item_def(ITEM_CATTAIL_RHIZOMES)->rot_days > item_def(ITEM_BLACKBERRIES)->rot_days);
    CHECK(item_def(ITEM_BLACKBERRIES)->rot_days > item_def(ITEM_NETTLES)->rot_days);
    /* Bodies rot too; a big one lies alone on its tile. */
    ItemTypeMask bodies = item_types_in_category(ITEM_CATEGORY_BODIES);
    CHECK(bodies & ITEM_TYPE_BIT(ITEM_SETTLER_BODY));
    CHECK(bodies & ITEM_TYPE_BIT(ITEM_HARE_CARCASS));
    CHECK(item_rots(ITEM_SETTLER_BODY) && !item_is_food(ITEM_SETTLER_BODY));
    CHECK_EQ_INT(item_stack_limit(ITEM_SETTLER_BODY), 1);
    CHECK(item_stack_limit(ITEM_HARE_CARCASS) > 1);
    CHECK(item_types_all() & ITEM_TYPE_BIT(ITEM_TYPE_COUNT - 1));
    /* Roasting gives more food. */
    CHECK(item_def(ITEM_ROASTED_RHIZOMES)->nutrition > item_def(ITEM_CATTAIL_RHIZOMES)->nutrition);
    CHECK(item_def(ITEM_ROASTED_CARROTS)->nutrition > item_def(ITEM_WILD_CARROTS)->nutrition);
}

static void test_rot_rate(void)
{
    CHECK_NEAR(item_rot_rate(ITEM_ROT_REFERENCE_CELSIUS, false), 1.0f, 1e-5f);
    CHECK_NEAR(item_rot_rate(ITEM_ROT_REFERENCE_CELSIUS + ITEM_ROT_DOUBLING_CELSIUS, false), 2.0f,
               1e-5f);
    CHECK_NEAR(item_rot_rate(ITEM_ROT_REFERENCE_CELSIUS - ITEM_ROT_DOUBLING_CELSIUS, false), 0.5f,
               1e-5f);
    CHECK_NEAR(item_rot_rate(ITEM_ROT_REFERENCE_CELSIUS, true), ITEM_ROT_SHELTERED, 1e-5f);
    CHECK_NEAR(item_rot_rate(80.0f, false), ITEM_ROT_RATE_MAX, 1e-5f);
    /* Frozen. */
    CHECK_NEAR(item_rot_rate(ITEM_ROT_FROZEN_CELSIUS, false), 0.0f, 1e-6f);
    CHECK_NEAR(item_rot_rate(-10.0f, true), 0.0f, 1e-6f);
    CHECK(item_rot_rate(0.5f, false) > 0.0f);
}

static void test_rot_hours(void)
{
    float days = item_def(ITEM_MUSHROOMS)->rot_days;
    ItemStack fresh = item_stack_pristine(ITEM_MUSHROOMS, 4);
    CHECK_NEAR(item_stack_rot_hours(fresh, 1.0f), days * 24.0f, 0.01f);
    CHECK_NEAR(item_stack_rot_hours(fresh, 2.0f), days * 12.0f, 0.01f);
    ItemStack bruised = item_stack_make(ITEM_MUSHROOMS, 4, ITEM_QUALITY_BEST,
                                        item_def(ITEM_MUSHROOMS)->max_hit_points / 2);
    CHECK(item_stack_rot_hours(bruised, 1.0f) < item_stack_rot_hours(fresh, 1.0f));
    CHECK(item_stack_rot_hours(fresh, 0.0f) < 0.0f);
    CHECK(item_stack_rot_hours(item_stack_pristine(ITEM_WOOD, 1), 1.0f) < 0.0f);
    CHECK(item_stack_rot_hours((ItemStack){0}, 1.0f) < 0.0f);
}

/* Rot rate for every tile of a test: a float behind the context pointer
 * (things dry). */
static ItemConditions fixed_rate(const void *context, size_t tile)
{
    (void)tile;
    return (ItemConditions){*(const float *)context, true};
}

/* Every tile out in the rain: nothing dries. */
static ItemConditions rained_on(const void *context, size_t tile)
{
    (void)context;
    (void)tile;
    return (ItemConditions){ITEM_ROT_RAINED_ON, false};
}

static void test_grass_does_not_dry_in_the_rain(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_FRESH_GRASS, 3)) == 3);
    int hours = item_def(ITEM_FRESH_GRASS)->transform_hours;
    CHECK_EQ_INT(items_advance(&items, 0, 16, hours, rained_on, NULL), 0);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_FRESH_GRASS);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).progress, 0);
    CHECK_EQ_INT(items_advance(&items, 0, 16, hours, NULL, NULL), 1);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_GRASS);
    items_free(&items);
}

static void test_twigs_get_wet_in_the_rain_and_dry_again(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_TWIGS, 3)) == 3);
    CHECK_EQ_INT(item_def(ITEM_TWIGS)->wets_into, ITEM_WET_TWIGS);
    CHECK_EQ_INT(item_def(ITEM_WET_TWIGS)->transforms_into, ITEM_TWIGS);
    /* Dry weather: they stay dry. */
    CHECK_EQ_INT(items_advance(&items, 0, 16, 48, NULL, NULL), 0);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_TWIGS);
    /* An hour in the rain: wet, and they do not dry while it lasts. */
    CHECK_EQ_INT(items_advance(&items, 0, 16, 1, rained_on, NULL), 1);
    ItemStack wet = items_get(&items, 1, 1, 0);
    CHECK_EQ_INT(wet.type, ITEM_WET_TWIGS);
    CHECK_EQ_INT(wet.count, 3);
    CHECK_EQ_INT((int)items.totals[ITEM_TWIGS], 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WET_TWIGS], 3);
    CHECK_EQ_INT(items_advance(&items, 0, 16, 48, rained_on, NULL), 0);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).progress, 0);
    /* Dry again in a day out of the rain. */
    int hours = item_def(ITEM_WET_TWIGS)->transform_hours;
    CHECK_EQ_INT(items_advance(&items, 0, 16, hours - 1, NULL, NULL), 0);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_WET_TWIGS);
    CHECK_EQ_INT(items_advance(&items, 0, 16, 1, NULL, NULL), 1);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_TWIGS);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).count, 3);
    /* Branches and logs do not soak through in a shower. */
    REQUIRE(items_add(&items, 2, 2, 0, item_stack_pristine(ITEM_BRANCHES, 2)) == 2);
    items_advance(&items, 0, 16, 1, rained_on, NULL);
    CHECK_EQ_INT(items_get(&items, 2, 2, 0).type, ITEM_BRANCHES);
    items_free(&items);
}

/* Stacks are counted per level, for searches to skip the empty ones. */
static void test_stacks_per_level(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 3));
    CHECK_EQ_UINT(items.level_stacks[0] + items.level_stacks[1] + items.level_stacks[2], 0);
    REQUIRE(items_add(&items, 1, 1, 2, item_stack_pristine(ITEM_WOOD, 5)) == 5);
    REQUIRE(items_add(&items, 1, 1, 2, item_stack_pristine(ITEM_WOOD, 5)) == 5); /* Same stack. */
    REQUIRE(items_add(&items, 2, 1, 2, item_stack_pristine(ITEM_MUSHROOMS, 5)) == 5);
    REQUIRE(items_add(&items, 0, 3, 0, item_stack_pristine(ITEM_WOOD, 1)) == 1);
    CHECK_EQ_UINT(items.level_stacks[0], 1);
    CHECK_EQ_UINT(items.level_stacks[1], 0);
    CHECK_EQ_UINT(items.level_stacks[2], 2);
    CHECK_EQ_INT(items_remove(&items, 1, 1, 2, 4), 4); /* Some left. */
    CHECK_EQ_UINT(items.level_stacks[2], 2);
    CHECK_EQ_INT(items_remove(&items, 1, 1, 2, 99), 6);
    CHECK_EQ_UINT(items.level_stacks[2], 1);
    /* Rotted away: gone from the count too. */
    int hours = (int)(item_def(ITEM_MUSHROOMS)->rot_days * 24.0f) + 2;
    CHECK_EQ_INT(items_advance(&items, 0, 48, hours, NULL, NULL), 1);
    CHECK_EQ_UINT(items.level_stacks[2], 0);
    CHECK_EQ_UINT(items.level_stacks[0], 1);
    items_free(&items);
}

static void test_food_rots_away(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_MUSHROOMS, 5)) == 5);
    REQUIRE(items_add(&items, 2, 1, 0, item_stack_pristine(ITEM_HAZELNUTS, 5)) == 5);
    REQUIRE(items_add(&items, 3, 1, 0, item_stack_pristine(ITEM_WOOD, 5)) == 5);
    int hours = (int)(item_def(ITEM_MUSHROOMS)->rot_days * 24.0f);
    int max = item_def(ITEM_MUSHROOMS)->max_hit_points;
    size_t tiles = 16;

    /* Half way: half the hit points are gone, the rest kept as progress. */
    for (int hour = 0; hour < hours / 2; hour++) {
        CHECK_EQ_INT(items_advance(&items, 0, tiles, 1, NULL, NULL), 0);
    }
    ItemStack half = items_get(&items, 1, 1, 0);
    CHECK_EQ_INT(half.count, 5);
    int left = half.hit_points * ITEM_ROT_STEP - half.progress;
    CHECK(abs(left - max * ITEM_ROT_STEP / 2) <= hours); /* Rounding, at most 1 an hour. */
    CHECK(items_get(&items, 2, 1, 0).hit_points < item_def(ITEM_HAZELNUTS)->max_hit_points);

    /* Frozen: nothing changes. */
    float frozen = 0.0f;
    CHECK_EQ_INT(items_advance(&items, 0, tiles, 24, fixed_rate, &frozen), 0);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).hit_points, half.hit_points);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).progress, half.progress);

    /* The rest of the way: gone, the totals with them. */
    int rotted = 0;
    for (int hour = 0; hour < hours / 2 + 1; hour++) {
        rotted += items_advance(&items, 0, tiles, 1, NULL, NULL);
    }
    CHECK_EQ_INT(rotted, 1);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_MUSHROOMS], 0);
    /* Nuts keep, wood does not rot. */
    CHECK_EQ_INT(items_get(&items, 2, 1, 0).count, 5);
    CHECK_EQ_INT(items_get(&items, 3, 1, 0).hit_points, item_def(ITEM_WOOD)->max_hit_points);

    /* Twice as fast when warmer. */
    REQUIRE(items_add(&items, 0, 0, 0, item_stack_pristine(ITEM_NETTLES, 2)) == 2);
    float warm = 2.0f;
    int nettle_hours = (int)(item_def(ITEM_NETTLES)->rot_days * 24.0f);
    for (int hour = 0; hour < nettle_hours / 2 - 1; hour++) {
        items_advance(&items, 0, 1, 1, fixed_rate, &warm);
    }
    CHECK_EQ_INT(items_get(&items, 0, 0, 0).type, ITEM_NETTLES);
    items_advance(&items, 0, 1, 2, fixed_rate, &warm);
    CHECK_EQ_INT(items_get(&items, 0, 0, 0).type, ITEM_NONE);
    items_free(&items);
}

static void test_rot_progress_merges(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    ItemStack old = item_stack_make(ITEM_BLACKBERRIES, 2, ITEM_QUALITY_STANDARD, 4);
    old.progress = 600;
    REQUIRE(items_add(&items, 1, 1, 0, old) == 2);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).progress, 600);
    ItemStack fresh = item_stack_make(ITEM_BLACKBERRIES, 2, ITEM_QUALITY_STANDARD, 4);
    REQUIRE(items_add(&items, 1, 1, 0, fresh) == 2);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).progress, 300);
    /* Out of range: clamped below one hit point. */
    ItemStack odd = item_stack_make(ITEM_HAZELNUTS, 1, ITEM_QUALITY_STANDARD, 50);
    odd.progress = 5000;
    REQUIRE(items_add(&items, 2, 2, 0, odd) == 1);
    CHECK(items_get(&items, 2, 2, 0).progress < ITEM_ROT_STEP);
    items_free(&items);
}

/* Eating a body takes weight, not hit points; the last bite empties it; what
 * is eaten is kept when the item is put back and averaged when stacks merge. */
static void test_eating_takes_weight_not_hit_points(void)
{
    ItemStack deer = item_stack_pristine(ITEM_ROE_DEER_CARCASS, 1);
    CHECK_NEAR(item_stack_mass(deer), 22.0f, 1e-4f);
    CHECK_NEAR(item_stack_eat(&deer, 5.5f), 5.5f, 1e-4f);
    CHECK_NEAR(item_stack_mass(deer), 16.5f, 0.01f);
    CHECK_EQ_INT(deer.hit_points, 100);
    CHECK_EQ_INT(deer.type, ITEM_ROE_DEER_CARCASS);
    /* A tiny bite still takes something. */
    ItemStack big = item_stack_pristine(ITEM_RED_DEER_CARCASS, 1);
    item_stack_eat(&big, 0.00001f);
    CHECK(big.eaten > 0);
    /* The last bite takes what is left, no more. */
    CHECK_NEAR(item_stack_eat(&deer, 50.0f), 16.5f, 0.01f);
    CHECK_EQ_INT(deer.type, ITEM_NONE);

    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    ItemStack half = item_stack_pristine(ITEM_HARE_CARCASS, 1);
    item_stack_eat(&half, 1.75f);
    REQUIRE(items_add(&items, 1, 1, 0, half) == 1);
    CHECK_NEAR(item_stack_mass(items_get(&items, 1, 1, 0)), 1.75f, 0.01f);
    /* A whole one next to it: the stack averages. */
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_HARE_CARCASS, 1)) == 1);
    CHECK_NEAR(item_stack_mass(items_get(&items, 1, 1, 0)), 2.0f * 3.5f * 0.75f, 0.01f);
    /* Eaten shares only mean something for bodies. */
    ItemStack wood = item_stack_pristine(ITEM_WOOD, 1);
    wood.eaten = 5000;
    REQUIRE(items_add(&items, 2, 2, 0, wood) == 1);
    CHECK_EQ_INT(items_get(&items, 2, 2, 0).eaten, 0);
    /* The hash sees it. */
    Items other;
    REQUIRE(items_init(&other, 4, 4, 1));
    items_add(&other, 1, 1, 0, item_stack_pristine(ITEM_HARE_CARCASS, 2));
    items_add(&other, 2, 2, 0, item_stack_pristine(ITEM_WOOD, 1));
    CHECK(items_hash(&items, HASH_FNV1A64_INIT) != items_hash(&other, HASH_FNV1A64_INIT));
    items_free(&other);
    items_free(&items);
}

static void test_hash(void)
{
    Items a, b;
    REQUIRE(items_init(&a, 8, 8, 1));
    REQUIRE(items_init(&b, 8, 8, 1));
    CHECK_EQ_UINT(items_hash(&a, HASH_FNV1A64_INIT), items_hash(&b, HASH_FNV1A64_INIT));
    items_add(&a, 1, 1, 0, item_stack_pristine(ITEM_WOOD, 2));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_add(&b, 1, 1, 0, item_stack_pristine(ITEM_WOOD, 3));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));

    /* Same items in another condition. */
    items_remove(&b, 1, 1, 0, 1);
    CHECK_EQ_UINT(items_hash(&a, HASH_FNV1A64_INIT), items_hash(&b, HASH_FNV1A64_INIT));
    items_remove(&b, 1, 1, 0, 2);
    items_add(&b, 1, 1, 0, item_stack_make(ITEM_WOOD, 2, TEST_QUALITY_FINE, 60));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_remove(&b, 1, 1, 0, 2);
    items_add(&b, 1, 1, 0, item_stack_make(ITEM_WOOD, 2, ITEM_QUALITY_BEST, 59));
    CHECK(items_hash(&a, HASH_FNV1A64_INIT) != items_hash(&b, HASH_FNV1A64_INIT));
    items_free(&a);
    items_free(&b);
}

/* Each item rots on its own clock: the older one goes first. */
static void test_items_keep_their_own_rot(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_BLACKBERRIES, 1)) == 1);
    float hours = item_stack_rot_hours(item_stack_pristine(ITEM_BLACKBERRIES, 1), 1.0f);
    REQUIRE(hours > 24.0f);
    items_advance(&items, 0, 16, 24, NULL, NULL);
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_BLACKBERRIES, 1)) == 1);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).count, 2);
    int left = (int)ceilf(hours) - 24;
    for (int hour = 0; hour < left; hour++) {
        items_advance(&items, 0, 16, 1, NULL, NULL);
    }
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).count, 1);
    const Item *fresh = items_first(&items, 1 * 4 + 1);
    REQUIRE(fresh != NULL);
    CHECK(items_next(&items, fresh) == NULL);
    CHECK(fresh->hit_points > 1);
    CHECK_EQ_INT(items.totals[ITEM_BLACKBERRIES], 1);
    CHECK_EQ_INT(items.item_count, 1);
    items_free(&items);
}

/* Items dry one by one: a tile holds dry and wet twigs for a while, and room
 * counts both. */
static void test_items_dry_one_by_one(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    int dry_hours = item_def(ITEM_WET_TWIGS)->transform_hours;
    REQUIRE(items_add(&items, 2, 2, 0, item_stack_pristine(ITEM_WET_TWIGS, 10)) == 10);
    items_advance(&items, 0, 16, dry_hours / 2, NULL, NULL);
    REQUIRE(items_add(&items, 2, 2, 0, item_stack_pristine(ITEM_WET_TWIGS, 5)) == 5);
    CHECK_EQ_INT(items_advance(&items, 0, 16, dry_hours - dry_hours / 2, NULL, NULL), 1);
    CHECK_EQ_INT(items_count_at(&items, 2, 2, 0, ITEM_TWIGS), 10);
    CHECK_EQ_INT(items_count_at(&items, 2, 2, 0, ITEM_WET_TWIGS), 5);
    CHECK_EQ_INT(items.totals[ITEM_TWIGS], 10);
    CHECK_EQ_INT(items.totals[ITEM_WET_TWIGS], 5);
    /* The summary is the newest item's type. */
    CHECK_EQ_INT(items_get(&items, 2, 2, 0).type, ITEM_WET_TWIGS);
    CHECK_EQ_INT(items_get(&items, 2, 2, 0).count, 5);
    int room = item_stack_limit(ITEM_WET_TWIGS) - 15;
    CHECK_EQ_INT(items_add(&items, 2, 2, 0, item_stack_pristine(ITEM_WET_TWIGS, 1000)), room);
    items_free(&items);
}

/* Dropped items lie apart on their tile, each turned its own way. */
static void test_items_lie_apart(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add(&items, 1, 2, 0, item_stack_pristine(ITEM_COBBLE, 1)) == 1);
    REQUIRE(items_add(&items, 1, 2, 0, item_stack_pristine(ITEM_COBBLE, 3)) == 3);
    const Item *list[4];
    int n = 0;
    for (const Item *item = items_first(&items, 2 * 4 + 1); item != NULL;
         item = items_next(&items, item)) {
        REQUIRE(n < 4);
        list[n++] = item;
    }
    CHECK_EQ_INT(n, 4);
    bool turned = false;
    for (int i = 0; i < n; i++) {
        CHECK(list[i]->spot_x >= 32 && list[i]->spot_x <= 224);
        CHECK(list[i]->spot_y >= 32 && list[i]->spot_y <= 224);
        CHECK_EQ_INT((int)list[i]->tile, 2 * 4 + 1);
        for (int j = 0; j < i; j++) {
            CHECK(list[i]->spot_x != list[j]->spot_x || list[i]->spot_y != list[j]->spot_y);
            turned |= list[i]->angle != list[j]->angle;
        }
    }
    CHECK(turned);
    items_free(&items);
}

/* A picture copies the very items, in order, and only what changed. */
static void test_picture_copies_items(void)
{
    Items real, picture;
    REQUIRE(items_init(&real, 4, 4, 1));
    REQUIRE(items_init(&picture, 4, 4, 1));
    picture.picture = true;
    REQUIRE(items_add(&real, 3, 1, 0, item_stack_make(ITEM_WOOD, 2, 20, 10)) == 2);
    REQUIRE(items_add(&real, 3, 1, 0, item_stack_make(ITEM_WOOD, 1, 80, 30)) == 1);
    size_t index = 1 * 4 + 3;
    items_copy_tile(&picture, &real, index);
    const Item *a = items_first(&real, index);
    const Item *b = items_first(&picture, index);
    int n = 0;
    while (a != NULL && b != NULL) {
        CHECK_EQ_INT(a->quality, b->quality);
        CHECK_EQ_INT(a->hit_points, b->hit_points);
        CHECK_EQ_INT(a->spot_x, b->spot_x);
        CHECK_EQ_INT(a->angle, b->angle);
        a = items_next(&real, a);
        b = items_next(&picture, b);
        n++;
    }
    CHECK(a == NULL && b == NULL);
    CHECK_EQ_INT(n, 3);
    CHECK_EQ_INT(picture.totals[ITEM_WOOD], 3);
    CHECK_EQ_INT((int)picture.level_stacks[0], 1);
    CHECK_EQ_INT(items_get(&picture, 3, 1, 0).quality, items_get(&real, 3, 1, 0).quality);
    /* Up to date: copying again changes nothing. */
    uint32_t used = picture.pool_used;
    items_copy_tile(&picture, &real, index);
    CHECK_EQ_INT((int)picture.pool_used, (int)used);
    /* Changed in the picture itself: copied again. */
    items_remove(&picture, 3, 1, 0, 3);
    items_copy_tile(&picture, &real, index);
    CHECK_EQ_INT(items_get(&picture, 3, 1, 0).count, 3);
    /* Gone from the real tile: gone from the picture. */
    items_remove(&real, 3, 1, 0, 3);
    items_copy_tile(&picture, &real, index);
    CHECK_EQ_INT(items_get(&picture, 3, 1, 0).type, ITEM_NONE);
    CHECK_EQ_INT((int)picture.level_stacks[0], 0);
    CHECK_EQ_INT(picture.item_count, 0);
    items_free(&real);
    items_free(&picture);
}

/* Slots of items gone are used again, with a new generation. */
static void test_item_slots_are_reused(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add(&items, 0, 0, 0, item_stack_pristine(ITEM_FLINT, 1)) == 1);
    CHECK_EQ_INT(items_first(&items, 0)->generation, 0);
    REQUIRE(items_remove(&items, 0, 0, 0, 1) == 1);
    REQUIRE(items_add(&items, 0, 3, 0, item_stack_pristine(ITEM_FLINT, 1)) == 1);
    CHECK_EQ_INT((int)items.pool_used, 1);
    CHECK_EQ_INT(items_first(&items, 3 * 4)->generation, 1);
    CHECK(items_first(&items, 0) == NULL);
    items_free(&items);
}

/* Wild items lie beside a stack without being part of it. */
static void test_wild_items_stay_apart(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    CHECK_EQ_INT(items_add_wild(&items, 1, 1, 0, ITEM_TWIGS, 3), 3);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_NONE);
    CHECK_EQ_INT(items.totals[ITEM_TWIGS], 0);
    CHECK_EQ_INT(items.wild_totals[ITEM_TWIGS], 3);
    CHECK_EQ_INT((int)items.level_stacks[0], 0);
    CHECK_EQ_INT((int)items.level_wild[0], 3);
    /* A stack of another type takes the tile all the same, its whole room. */
    int limit = item_stack_limit(ITEM_WOOD);
    CHECK_EQ_INT(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_WOOD, limit)), limit);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).type, ITEM_WOOD);
    CHECK_EQ_INT(items_get(&items, 1, 1, 0).count, limit);
    CHECK_EQ_INT(items_wild_count(&items, 1, 1, 0, ITEM_TWIGS), 3);
    /* Twigs added as a stack do not take the wild ones in. */
    CHECK_EQ_INT(items_add(&items, 2, 1, 0, item_stack_pristine(ITEM_TWIGS, 2)), 2);
    CHECK_EQ_INT(items_add_wild(&items, 2, 1, 0, ITEM_TWIGS, 1), 1);
    CHECK_EQ_INT(items_get(&items, 2, 1, 0).count, 2);
    CHECK_EQ_INT(items_remove(&items, 2, 1, 0, 10), 2);
    CHECK_EQ_INT(items_wild_count(&items, 2, 1, 0, ITEM_TWIGS), 1);
    /* Forbidding is for stacks. */
    CHECK(!items_set_forbidden(&items, 2, 1, 0, true));
    CHECK_EQ_INT(items_remove_wild(&items, 1, 1, 0, ITEM_TWIGS, 2), 2);
    CHECK_EQ_INT(items.wild_totals[ITEM_TWIGS], 2);
    CHECK_EQ_INT(items.item_count, limit + 2);
    items_free(&items);
}

/* Picked up, wild items are wild no more, and go where they are put as they
 * were. */
static void test_wild_items_picked_up(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    CHECK_EQ_INT(items_add_wild(&items, 0, 0, 0, ITEM_FLINT, 6), 6);
    int best = 0;
    bool varied = false;
    for (const Item *item = items_first(&items, 0); item != NULL; item = items_next(&items, item)) {
        varied |= item->quality != items_first(&items, 0)->quality;
        best = item->quality > best ? item->quality : best;
    }
    CHECK(varied); /* Each its own quality. */
    ItemStack load = {.type = ITEM_NONE};
    CHECK_EQ_INT(items_pick_up_wild(&items, 0, 0, 0, ITEM_FLINT, 1, true, ITEM_NONE, &load), 1);
    CHECK_EQ_INT(load.type, ITEM_FLINT);
    CHECK_EQ_INT(load.quality, best);
    CHECK_EQ_INT(items.held_count, 1);
    CHECK_EQ_INT(items.wild_totals[ITEM_FLINT], 5);
    const Item *held = items_load_first(&items, load);
    REQUIRE(held != NULL);
    CHECK_EQ_INT(held->flags & ITEM_FLAG_WILD, 0);
    CHECK_EQ_INT(items_put(&items, 3, 3, 0, &load), 1);
    CHECK_EQ_INT(load.count, 0);
    CHECK_EQ_INT(items_get(&items, 3, 3, 0).quality, best);
    CHECK_EQ_INT(items.totals[ITEM_FLINT], 1);
    /* Twigs picked up from wet ground are wet twigs. */
    CHECK_EQ_INT(items_add_wild(&items, 1, 0, 0, ITEM_TWIGS, 2), 2);
    CHECK_EQ_INT(items_pick_up_wild(&items, 1, 0, 0, ITEM_TWIGS, 5, false, ITEM_WET_TWIGS, &load),
                 2);
    CHECK_EQ_INT(load.type, ITEM_WET_TWIGS);
    CHECK_EQ_INT(load.count, 2);
    /* Not into a load of something else. */
    CHECK_EQ_INT(items_pick_up_wild(&items, 0, 0, 0, ITEM_FLINT, 1, true, ITEM_NONE, &load), 0);
    items_use_up(&items, &load, 2);
    CHECK_EQ_INT(items.held_count, 0);
    items_free(&items);
}

/* Wild branches and twigs crumble away in time; stones stay. */
static void test_wild_litter_crumbles(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add_wild(&items, 0, 0, 0, ITEM_TWIGS, 2) == 2);
    REQUIRE(items_add_wild(&items, 1, 0, 0, ITEM_BRANCHES, 1) == 1);
    REQUIRE(items_add_wild(&items, 2, 0, 0, ITEM_COBBLE, 1) == 1);
    int twig_hours = (int)(item_def(ITEM_TWIGS)->litter_days * 24.0f);
    int branch_hours = (int)(item_def(ITEM_BRANCHES)->litter_days * 24.0f);
    REQUIRE(twig_hours > 0 && branch_hours > twig_hours);
    for (int hour = 0; hour < twig_hours - 1; hour++) {
        items_advance(&items, 0, 16, 1, NULL, NULL);
    }
    CHECK_EQ_INT(items_wild_count(&items, 0, 0, 0, ITEM_TWIGS),
                 2); /* Not wet: wild twigs only crumble. */
    items_advance(&items, 0, 16, 1, NULL, NULL);
    CHECK_EQ_INT(items_wild_count(&items, 0, 0, 0, ITEM_TWIGS), 0);
    CHECK_EQ_INT(items_wild_count(&items, 1, 0, 0, ITEM_BRANCHES), 1);
    items_advance(&items, 0, 16, branch_hours - twig_hours, NULL, NULL);
    CHECK_EQ_INT(items_wild_count(&items, 1, 0, 0, ITEM_BRANCHES), 0);
    CHECK_EQ_INT(items_wild_count(&items, 2, 0, 0, ITEM_COBBLE), 1);
    CHECK_EQ_INT((int)items.level_wild[0], 1);
    items_free(&items);
}

/* An item is found where it is drawn, and moved and turned by hand. */
static void test_item_picked_by_point_moved_and_turned(void)
{
    Items items;
    REQUIRE(items_init(&items, 8, 8, 2));
    REQUIRE(items_add(&items, 3, 3, 0, item_stack_pristine(ITEM_WOOD, 1)) == 1);
    const Item *log = items_first(&items, 3 * 8 + 3);
    REQUIRE(log != NULL);
    float cx = 3.0f + ((float)log->spot_x + 0.5f) / 256.0f;
    float cy = 3.0f + ((float)log->spot_y + 0.5f) / 256.0f;
    ItemId id = items_at_point(&items, cx, cy, 0);
    CHECK_EQ_UINT(id.slot, items_id(&items, log).slot);
    CHECK(items_by_id(&items, id) == log);
    CHECK_EQ_UINT(items_at_point(&items, cx, cy, 1).slot, UINT32_MAX); /* Another level. */
    CHECK_EQ_UINT(items_at_point(&items, 6.5f, 6.5f, 0).slot, UINT32_MAX);
    /* Moved onto a tile holding something else, at the very spot. */
    REQUIRE(items_add(&items, 5, 4, 0, item_stack_pristine(ITEM_FLINT, 2)) == 2);
    REQUIRE(items_move(&items, id, 5, 4, 0, 200, 30));
    CHECK_EQ_INT(items_count_at(&items, 3, 3, 0, ITEM_NONE), 0);
    CHECK_EQ_INT((int)items.level_stacks[0], 1);
    log = items_by_id(&items, id);
    REQUIRE(log != NULL);
    CHECK_EQ_INT((int)log->tile, 4 * 8 + 5);
    CHECK_EQ_INT(log->spot_x, 200);
    CHECK_EQ_INT(log->spot_y, 30);
    CHECK_EQ_INT(items_count_at(&items, 5, 4, 0, ITEM_FLINT), 2);
    CHECK_EQ_INT(items.totals[ITEM_WOOD], 1);
    CHECK_EQ_UINT(items_at_point(&items, 5.0f + 200.5f / 256.0f, 4.0f + 30.5f / 256.0f, 0).slot,
                  id.slot);
    /* Turned. */
    int angle = log->angle;
    REQUIRE(items_turn(&items, id, 10));
    CHECK_EQ_INT(items_by_id(&items, id)->angle, (angle + 10) & 255);
    /* Gone: the id names nothing any more, even once the slot is used again. */
    ItemStack load = {.type = ITEM_NONE};
    REQUIRE(items_pick_up(&items, 5, 4, 0, 1, &load) == 1); /* The newest: the log. */
    CHECK(items_by_id(&items, id) == NULL);                 /* In hand. */
    items_use_up(&items, &load, 1);
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_WOOD, 1)) == 1);
    CHECK(items_by_id(&items, id) == NULL);
    CHECK(!items_move(&items, id, 2, 2, 0, 0, 0));
    CHECK(!items_turn(&items, id, 1));
    items_free(&items);
}

/* The items of a heap listed by quality, condition or rot. */
static void test_items_listed_in_order(void)
{
    Items items;
    REQUIRE(items_init(&items, 4, 4, 1));
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_make(ITEM_BLACKBERRIES, 1, 30, 9)) == 1);
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_make(ITEM_BLACKBERRIES, 1, 80, 4)) == 1);
    REQUIRE(items_add(&items, 1, 1, 0, item_stack_make(ITEM_BLACKBERRIES, 1, 55, 6)) == 1);
    size_t index = 1 * 4 + 1;
    const Item *out[3];
    CHECK_EQ_INT(items_list(&items, index, ITEM_ORDER_NEWEST, out, 3), 3);
    CHECK_EQ_INT(out[0]->quality, 55);
    CHECK_EQ_INT(out[2]->quality, 30);
    items_list(&items, index, ITEM_ORDER_QUALITY, out, 3);
    CHECK_EQ_INT(out[0]->quality, 80);
    CHECK_EQ_INT(out[1]->quality, 55);
    CHECK_EQ_INT(out[2]->quality, 30);
    items_list(&items, index, ITEM_ORDER_CONDITION, out, 3);
    CHECK_EQ_INT(out[0]->hit_points, 9);
    CHECK_EQ_INT(out[2]->hit_points, 4);
    items_list(&items, index, ITEM_ORDER_ROT, out, 3); /* The most rotten first. */
    CHECK_EQ_INT(out[0]->hit_points, 4);
    CHECK_EQ_INT(out[1]->hit_points, 6);
    CHECK_EQ_INT(item_rot_left(out[0]), 4 * ITEM_ROT_STEP);
    /* Fewer kept than lie there: the first ones of the order. */
    const Item *best[1];
    CHECK_EQ_INT(items_list(&items, index, ITEM_ORDER_QUALITY, best, 1), 3);
    CHECK_EQ_INT(best[0]->quality, 80);
    /* What never rots comes last. */
    REQUIRE(items_add_wild(&items, 1, 1, 0, ITEM_COBBLE, 1) == 1);
    const Item *all[4];
    CHECK_EQ_INT(items_list(&items, index, ITEM_ORDER_ROT, all, 4), 4);
    CHECK_EQ_INT(all[3]->type, ITEM_COBBLE);
    CHECK_EQ_INT(item_rot_left(all[3]), -1);
    CHECK_EQ_INT(item_as_stack(all[0]).count, 1);
    items_free(&items);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_stack_limits_follow_bulk);
    RUN_TEST(test_categories);
    RUN_TEST(test_additions_counter);
    RUN_TEST(test_add_and_remove);
    RUN_TEST(test_stacks_per_level);
    RUN_TEST(test_condition_definitions);
    RUN_TEST(test_quality_is_a_value_from_0_to_100);
    RUN_TEST(test_stack_make_keeps_condition_valid);
    RUN_TEST(test_hit_points_percent);
    RUN_TEST(test_same_type_stacks_whatever_the_quality);
    RUN_TEST(test_stack_merge);
    RUN_TEST(test_merged_hit_points_stay_between);
    RUN_TEST(test_drop_keeps_condition);
    RUN_TEST(test_drop_skips_other_types);
    RUN_TEST(test_drop_spreads_to_reachable_tiles);
    RUN_TEST(test_drop_without_room_loses_the_rest);
    RUN_TEST(test_rot_definitions);
    RUN_TEST(test_rot_rate);
    RUN_TEST(test_rot_hours);
    RUN_TEST(test_food_rots_away);
    RUN_TEST(test_rot_progress_merges);
    RUN_TEST(test_grass_does_not_dry_in_the_rain);
    RUN_TEST(test_twigs_get_wet_in_the_rain_and_dry_again);
    RUN_TEST(test_eating_takes_weight_not_hit_points);
    RUN_TEST(test_hash);
    RUN_TEST(test_items_keep_their_own_rot);
    RUN_TEST(test_items_dry_one_by_one);
    RUN_TEST(test_items_lie_apart);
    RUN_TEST(test_picture_copies_items);
    RUN_TEST(test_item_slots_are_reused);
    RUN_TEST(test_wild_items_stay_apart);
    RUN_TEST(test_wild_items_picked_up);
    RUN_TEST(test_wild_litter_crumbles);
    RUN_TEST(test_item_picked_by_point_moved_and_turned);
    RUN_TEST(test_items_listed_in_order);
    return test_summary();
}
