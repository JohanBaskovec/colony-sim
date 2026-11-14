/* Bed ownership (core/bed.h). */
#include "core/bed.h"

#include "test_framework.h"

static void test_assigning(void)
{
    Beds beds;
    REQUIRE(beds_init(&beds, 16, 16));
    TilePos bed;
    CHECK(!beds_owned(&beds, 1, &bed));
    CHECK_EQ_INT(beds_owner(&beds, 2, 2), BED_NO_OWNER);
    CHECK(!beds_assign(&beds, BED_NO_OWNER, 2, 2));
    CHECK(!beds_assign(&beds, 1, 16, 2));

    REQUIRE(beds_assign(&beds, 1, 2, 2));
    CHECK(beds_owned(&beds, 1, &bed) && bed.x == 2 && bed.y == 2);
    CHECK_EQ_INT(beds_owner(&beds, 2, 2), 1);
    /* One bed per owner: the old one is freed. */
    REQUIRE(beds_assign(&beds, 1, 5, 5));
    CHECK_EQ_INT(beds_owner(&beds, 2, 2), BED_NO_OWNER);
    CHECK_EQ_INT(beds_owner(&beds, 5, 5), 1);
    /* One owner per bed: the former owner loses it. */
    REQUIRE(beds_assign(&beds, 7, 5, 5));
    CHECK(!beds_owned(&beds, 1, &bed));
    CHECK_EQ_INT(beds_owner(&beds, 5, 5), 7);

    beds_release_bed(&beds, 5, 5);
    CHECK(!beds_owned(&beds, 7, &bed));
    REQUIRE(beds_assign(&beds, 255, 3, 3));
    beds_release_owner(&beds, 255);
    CHECK_EQ_INT(beds_owner(&beds, 3, 3), BED_NO_OWNER);
    beds_free(&beds);
}

static void test_forgetting_missing_beds(void)
{
    Map map;
    Beds beds;
    REQUIRE(map_init(&map, 16, 16));
    REQUIRE(beds_init(&beds, 16, 16));
    map_set_structure(&map, 2, 2, STRUCTURE_GRASS_BED);
    map_set_structure(&map, 6, 2, STRUCTURE_GRASS_BED);
    beds_assign(&beds, 1, 2, 2);
    beds_assign(&beds, 2, 6, 2);
    beds_assign(&beds, 3, 6, 3); /* The foot of a bed is not a bed's head. */
    uint64_t hash = beds_hash(&beds, 0);
    CHECK_EQ_INT(beds_forget_missing(&beds, &map), 1);
    CHECK(beds_hash(&beds, 0) != hash);
    map_set_structure(&map, 2, 3, STRUCTURE_NONE);
    CHECK_EQ_INT(beds_forget_missing(&beds, &map), 1);
    CHECK_EQ_INT(beds_owner(&beds, 2, 2), BED_NO_OWNER);
    CHECK_EQ_INT(beds_owner(&beds, 6, 2), 2);
    /* A wall where a bed was. */
    map_set_structure(&map, 6, 2, STRUCTURE_WALL);
    CHECK_EQ_INT(beds_forget_missing(&beds, &map), 1);
    CHECK_EQ_INT(beds_forget_missing(&beds, &map), 0);
    beds_free(&beds);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_assigning);
    RUN_TEST(test_forgetting_missing_beds);
    return test_summary();
}
