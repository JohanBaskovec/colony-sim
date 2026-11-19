#include "core/item_move.h"

#include "test_framework.h"
#include <math.h>

static TilePos tile(int x, int y)
{
    return (TilePos){(int16_t)x, (int16_t)y, 0};
}

static void test_add_find_replace_remove(void)
{
    ItemMoves moves;
    item_moves_init(&moves);
    CHECK_EQ_INT(item_moves_find(&moves, tile(1, 1)), -1);
    CHECK(item_moves_add(&moves, tile(1, 1), tile(5, 5)));
    CHECK(item_moves_add(&moves, tile(2, 2), tile(6, 6)));
    CHECK_EQ_INT(moves.count, 2);
    int index = item_moves_find(&moves, tile(2, 2));
    REQUIRE(index >= 0);
    CHECK_EQ_INT(moves.orders[index].destination.x, 6);
    /* A second order on the same source replaces the first. */
    uint32_t revision = moves.revision;
    CHECK(item_moves_add(&moves, tile(2, 2), tile(9, 9)));
    CHECK_EQ_INT(moves.count, 2);
    CHECK_EQ_INT(moves.orders[item_moves_find(&moves, tile(2, 2))].destination.x, 9);
    CHECK(moves.revision > revision);
    CHECK(item_moves_remove(&moves, tile(1, 1)));
    CHECK(!item_moves_remove(&moves, tile(1, 1)));
    CHECK_EQ_INT(moves.count, 1);
    CHECK_EQ_INT(item_moves_find(&moves, tile(2, 2)), 0);
}

static void test_refuses_same_tile_and_a_full_list(void)
{
    ItemMoves moves;
    item_moves_init(&moves);
    CHECK(!item_moves_add(&moves, tile(3, 3), tile(3, 3)));
    CHECK_EQ_INT(moves.count, 0);
    for (int i = 0; i < ITEM_MOVE_MAX; i++) {
        CHECK(item_moves_add(&moves, tile(i, 0), tile(i, 1)));
    }
    CHECK(!item_moves_add(&moves, tile(0, 5), tile(1, 5)));
    CHECK(item_moves_add(&moves, tile(0, 0), tile(7, 7))); /* Replacing is fine. */
    CHECK_EQ_INT(moves.count, ITEM_MOVE_MAX);
}

static bool always_busy(const void *context, const ItemMove *order)
{
    (void)context;
    (void)order;
    return true;
}

static void test_prune_drops_emptied_sources(void)
{
    Items items;
    REQUIRE(items_init(&items, 16, 16, 1));
    items_add(&items, 1, 1, 0, item_stack_pristine(ITEM_COBBLE, 4));
    items_add(&items, 2, 2, 0, item_stack_pristine(ITEM_COBBLE, 4));
    ItemMoves moves;
    item_moves_init(&moves);
    item_moves_add(&moves, tile(1, 1), tile(8, 8));
    item_moves_add(&moves, tile(2, 2), tile(9, 9));
    item_moves_add(&moves, tile(3, 3), tile(10, 10)); /* Nothing there. */
    CHECK_EQ_INT(item_moves_prune(&moves, &items, NULL, NULL), 1);
    CHECK_EQ_INT(moves.count, 2);
    items_remove(&items, 1, 1, 0, 4);
    CHECK_EQ_INT(item_moves_prune(&moves, &items, NULL, NULL), 1);
    CHECK_EQ_INT(moves.count, 1);
    CHECK_EQ_INT(moves.orders[0].source.x, 2);
    CHECK_EQ_INT(item_moves_prune(&moves, &items, NULL, NULL), 0);
    /* An order a settler is busy with stays while its source is empty. */
    items_remove(&items, 2, 2, 0, 4);
    CHECK_EQ_INT(item_moves_prune(&moves, &items, always_busy, NULL), 0);
    CHECK_EQ_INT(moves.count, 1);
    CHECK_EQ_INT(item_moves_prune(&moves, &items, NULL, NULL), 1);
    items_free(&items);
}

static void test_room_of_a_destination(void)
{
    Items items;
    REQUIRE(items_init(&items, 16, 16, 1));
    ItemStack cobbles = item_stack_pristine(ITEM_COBBLE, 3);
    int limit = item_stack_limit(ITEM_COBBLE);
    CHECK_EQ_INT(item_move_room(&items, tile(4, 4), cobbles), limit); /* Empty. */
    items_add(&items, 4, 4, 0, item_stack_pristine(ITEM_COBBLE, 5));
    CHECK_EQ_INT(item_move_room(&items, tile(4, 4), cobbles), limit - 5); /* Matching. */
    items_add(&items, 5, 5, 0, item_stack_pristine(ITEM_BRANCHES, 2));
    /* Something else: the room it leaves. */
    int taken = (int)ceilf(2.0f * item_def(ITEM_BRANCHES)->bulk / item_def(ITEM_COBBLE)->bulk);
    CHECK_EQ_INT(item_move_room(&items, tile(5, 5), cobbles), limit - taken);
    items_add(&items, 7, 7, 0, item_stack_pristine(ITEM_BRANCHES, 1000));
    CHECK_EQ_INT(item_move_room(&items, tile(7, 7), cobbles), 0); /* Full of something else. */
    items_add(&items, 6, 6, 0, item_stack_pristine(ITEM_COBBLE, limit));
    CHECK_EQ_INT(item_move_room(&items, tile(6, 6), cobbles), 0); /* Full. */
    CHECK_EQ_INT(item_move_room(&items, tile(4, 4), (ItemStack){.type = ITEM_NONE}), 0);
    items_free(&items);
}

static void test_hash_follows_the_orders(void)
{
    ItemMoves a, b;
    item_moves_init(&a);
    item_moves_init(&b);
    CHECK(item_moves_hash(&a, 1) == item_moves_hash(&b, 1));
    item_moves_add(&a, tile(1, 1), tile(2, 2));
    CHECK(item_moves_hash(&a, 1) != item_moves_hash(&b, 1));
    item_moves_add(&b, tile(1, 1), tile(2, 2));
    CHECK(item_moves_hash(&a, 1) == item_moves_hash(&b, 1));
    item_moves_add(&b, tile(1, 1), tile(3, 3));
    CHECK(item_moves_hash(&a, 1) != item_moves_hash(&b, 1));
}

int main(void)
{
    RUN_TEST(test_add_find_replace_remove);
    RUN_TEST(test_refuses_same_tile_and_a_full_list);
    RUN_TEST(test_prune_drops_emptied_sources);
    RUN_TEST(test_room_of_a_destination);
    RUN_TEST(test_hash_follows_the_orders);
    return test_summary();
}
