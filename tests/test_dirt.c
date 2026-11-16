/* Dirt layer: adding, spreading, cleaning, where dirt may lie. */
#include "core/dirt.h"
#include "core/util.h"

#include "test_framework.h"

static bool init_world(Map *map, Dirt *dirt)
{
    if (!map_init(map, 16, 16, 1) || !dirt_init(dirt, 16, 16, 1)) {
        return false;
    }
    map->terrain[map_index(map, 0, 0, 0)] = TERRAIN_SHALLOW_WATER;
    map->terrain[map_index(map, 1, 0, 0)] = TERRAIN_DEEP_WATER;
    map->rock[map_index(map, 2, 0, 0)] = ROCK_GRANITE;
    return true;
}

static void free_world(Map *map, Dirt *dirt)
{
    dirt_free(dirt);
    map_free(map);
}

static void test_init(void)
{
    Dirt dirt;
    CHECK(!dirt_init(&dirt, 0, 4, 1));
    dirt_free(&dirt); /* Safe after a failure. */
    REQUIRE(dirt_init(&dirt, 4, 4, 1));
    CHECK_EQ_INT(dirt.dirty_count, 0);
    CHECK_EQ_INT(dirt_at(&dirt, 3, 3, 0), 0);
    CHECK_EQ_INT(dirt_at(&dirt, -1, 0, 0), 0);
    CHECK_EQ_INT(dirt_at(&dirt, 4, 0, 0), 0);
    dirt_free(&dirt);
}

static void test_add_and_clean(void)
{
    Map map;
    Dirt dirt;
    REQUIRE(init_world(&map, &dirt));
    uint32_t revision = dirt.revision;
    CHECK_EQ_INT(dirt_add(&dirt, &map, 4, 4, 0, 30), 30);
    CHECK_EQ_INT(dirt_at(&dirt, 4, 4, 0), 30);
    CHECK_EQ_INT(dirt.dirty_count, 1);
    CHECK(dirt.revision != revision);
    /* Capped at DIRT_MAX. */
    CHECK_EQ_INT(dirt_add(&dirt, &map, 4, 4, 0, 90), DIRT_MAX - 30);
    CHECK_EQ_INT(dirt_at(&dirt, 4, 4, 0), DIRT_MAX);
    CHECK_EQ_INT(dirt.dirty_count, 1);
    revision = dirt.revision;
    CHECK_EQ_INT(dirt_add(&dirt, &map, 4, 4, 0, 5), 0);
    CHECK_EQ_INT(dirt_add(&dirt, &map, 5, 4, 0, 0), 0);
    CHECK_EQ_INT(dirt.revision, revision);

    /* Not on water, rock or outside the map. */
    CHECK(!dirt_can_lie(&map, 0, 0, 0));
    CHECK(!dirt_can_lie(&map, 1, 0, 0));
    CHECK(!dirt_can_lie(&map, 2, 0, 0));
    CHECK(dirt_can_lie(&map, 3, 0, 0));
    CHECK_EQ_INT(dirt_add(&dirt, &map, 0, 0, 0, 10), 0);
    CHECK_EQ_INT(dirt_add(&dirt, &map, 2, 0, 0, 10), 0);
    CHECK_EQ_INT(dirt_add(&dirt, &map, 16, 0, 0, 10), 0);

    CHECK_EQ_INT(dirt_clean(&dirt, 4, 4, 0, 40), 60);
    CHECK_EQ_INT(dirt.dirty_count, 1);
    CHECK_EQ_INT(dirt_clean(&dirt, 4, 4, 0, 100), 0);
    CHECK_EQ_INT(dirt.dirty_count, 0);
    CHECK_EQ_INT(dirt_clean(&dirt, 4, 4, 0, 10), 0);
    CHECK_EQ_INT(dirt.dirty_count, 0);
    CHECK_EQ_INT(dirt_clean(&dirt, -3, 4, 0, 10), 0);
    free_world(&map, &dirt);
}

static void test_spread(void)
{
    Map map;
    Dirt dirt;
    REQUIRE(init_world(&map, &dirt));
    dirt_spread(&dirt, &map, 4, 4, 0, 10);
    CHECK_EQ_INT(dirt_at(&dirt, 4, 4, 0), 10);
    CHECK_EQ_INT(dirt_at(&dirt, 3, 3, 0), 5);
    CHECK_EQ_INT(dirt_at(&dirt, 5, 4, 0), 5);
    CHECK_EQ_INT(dirt_at(&dirt, 6, 4, 0), 0);
    CHECK_EQ_INT(dirt.dirty_count, 9);
    /* Next to water and rock, only open ground gets dirty. */
    dirt_spread(&dirt, &map, 1, 1, 0, 10);
    CHECK_EQ_INT(dirt_at(&dirt, 0, 0, 0), 0);
    CHECK_EQ_INT(dirt_at(&dirt, 2, 0, 0), 0);
    CHECK_EQ_INT(dirt_at(&dirt, 1, 1, 0), 10);
    CHECK_EQ_INT(dirt_at(&dirt, 3, 0, 0), 0);
    CHECK_EQ_INT(dirt.dirty_count, 9 + 6);
    free_world(&map, &dirt);
}

static void test_hash(void)
{
    Map map;
    Dirt dirt;
    REQUIRE(init_world(&map, &dirt));
    uint64_t clean = dirt_hash(&dirt, HASH_FNV1A64_INIT);
    dirt_add(&dirt, &map, 4, 4, 0, 1);
    CHECK(dirt_hash(&dirt, HASH_FNV1A64_INIT) != clean);
    dirt_clean(&dirt, 4, 4, 0, 1);
    CHECK(dirt_hash(&dirt, HASH_FNV1A64_INIT) == clean);
    free_world(&map, &dirt);
}

int main(void)
{
    RUN_TEST(test_init);
    RUN_TEST(test_add_and_clean);
    RUN_TEST(test_spread);
    RUN_TEST(test_hash);
    return test_summary();
}
