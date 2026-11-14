#include "core/pool.h"

#include "test_framework.h"

static void test_init_rejects_zero_capacity(void)
{
    Pool pool;
    CHECK(!pool_init(&pool, 0));
}

static void test_alloc_gives_distinct_live_ids(void)
{
    Pool pool;
    REQUIRE(pool_init(&pool, 4));
    EntityId a = pool_alloc(&pool);
    EntityId b = pool_alloc(&pool);
    CHECK(!entity_id_is_none(a));
    CHECK(!entity_id_is_none(b));
    CHECK(!entity_id_equal(a, b));
    CHECK(pool_is_alive(&pool, a));
    CHECK(pool_is_alive(&pool, b));
    CHECK_EQ_UINT(pool.count, 2);
    pool_free(&pool);
}

static void test_full_pool_returns_none(void)
{
    Pool pool;
    REQUIRE(pool_init(&pool, 2));
    pool_alloc(&pool);
    pool_alloc(&pool);
    CHECK(entity_id_is_none(pool_alloc(&pool)));
    pool_free(&pool);
}

static void test_released_handle_becomes_stale(void)
{
    Pool pool;
    REQUIRE(pool_init(&pool, 2));
    EntityId old = pool_alloc(&pool);
    CHECK(pool_release(&pool, old));
    CHECK(!pool_is_alive(&pool, old));
    CHECK(!pool_release(&pool, old)); /* Double release is rejected. */

    /* The slot is reused with a new generation: the old handle stays invalid. */
    EntityId reused = pool_alloc(&pool);
    CHECK_EQ_UINT(reused.index, old.index);
    CHECK(reused.generation != old.generation);
    CHECK(pool_is_alive(&pool, reused));
    CHECK(!pool_is_alive(&pool, old));
    CHECK_EQ_UINT(pool.count, 1);
    pool_free(&pool);
}

static void test_invalid_handles(void)
{
    Pool pool;
    REQUIRE(pool_init(&pool, 2));
    CHECK(!pool_is_alive(&pool, ENTITY_NONE));
    CHECK(!pool_is_alive(&pool, (EntityId){1, 1})); /* Never allocated. */
    CHECK(!pool_is_alive(&pool, (EntityId){99, 1}));
    CHECK(!pool_release(&pool, ENTITY_NONE));
    pool_free(&pool);
}

static void test_iteration_by_slot(void)
{
    Pool pool;
    REQUIRE(pool_init(&pool, 8));
    EntityId ids[5];
    for (int i = 0; i < 5; i++) {
        ids[i] = pool_alloc(&pool);
    }
    pool_release(&pool, ids[1]);
    pool_release(&pool, ids[3]);

    int live = 0;
    for (uint32_t slot = 0; slot < pool.high_water; slot++) {
        EntityId id = pool_id_at(&pool, slot);
        if (!entity_id_is_none(id)) {
            CHECK(pool_is_alive(&pool, id));
            live++;
        }
    }
    CHECK_EQ_INT(live, 3);
    CHECK(entity_id_is_none(pool_id_at(&pool, 1)));
    CHECK(entity_id_is_none(pool_id_at(&pool, 7)));
    pool_free(&pool);
}

int main(void)
{
    RUN_TEST(test_init_rejects_zero_capacity);
    RUN_TEST(test_alloc_gives_distinct_live_ids);
    RUN_TEST(test_full_pool_returns_none);
    RUN_TEST(test_released_handle_becomes_stale);
    RUN_TEST(test_invalid_handles);
    RUN_TEST(test_iteration_by_slot);
    return test_summary();
}
