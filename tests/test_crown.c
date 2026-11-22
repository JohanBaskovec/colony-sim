/* What hangs in the crowns of the trees (core/crown.h). */
#include "core/crown.h"

#include "core/game_clock.h"
#include "core/tree.h"
#include "core/util.h"

#include "test_framework.h"

#define SIZE 16
#define DEPTH 4

/* Soil on level 0, open air over it; foliage of each density on level 2
 * over (4..6, 4): dense, medium, sparse. */
static bool init_world(Map *map, Items *items, Crowns *crowns)
{
    if (!map_init(map, SIZE, SIZE, DEPTH) || !items_init(items, SIZE, SIZE, DEPTH) ||
        !crowns_init(crowns, SIZE, SIZE, DEPTH)) {
        return false;
    }
    map->terrain[map_index(map, 4, 4, 2)] = TERRAIN_FOLIAGE;
    map->terrain[map_index(map, 5, 4, 2)] = TERRAIN_FOLIAGE_MEDIUM;
    map->terrain[map_index(map, 6, 4, 2)] = TERRAIN_FOLIAGE_SPARSE;
    map->litter = items;
    map->crowns = crowns;
    return true;
}

static void free_world(Map *map, Items *items, Crowns *crowns)
{
    crowns_free(crowns);
    items_free(items);
    map_free(map);
}

static CrownItem hanging(ItemType type, int quality, int day)
{
    return (CrownItem){.type = (uint8_t)type,
                       .quality = (uint8_t)quality,
                       .hit_points = (uint16_t)item_def(type)->max_hit_points,
                       .day = (uint16_t)day};
}

/* Foliage holds by its density, and only foliage. */
static void test_room(void)
{
    Map map;
    Items items;
    Crowns crowns;
    REQUIRE(init_world(&map, &items, &crowns));
    CHECK(crown_room(TERRAIN_FOLIAGE) > crown_room(TERRAIN_FOLIAGE_MEDIUM));
    CHECK(crown_room(TERRAIN_FOLIAGE_MEDIUM) > crown_room(TERRAIN_FOLIAGE_SPARSE));
    CHECK(crown_room(TERRAIN_FOLIAGE_SPARSE) > 0.0f);
    CHECK(crown_room(TERRAIN_OPEN) == 0.0f && crown_room(TERRAIN_SOIL) == 0.0f);
    float apple = item_def(ITEM_APPLES)->bulk;
    CHECK_EQ_INT(crowns_room(&crowns, &map, 4, 4, 2, ITEM_APPLES),
                 (int)(CROWN_ROOM_DENSE / apple + 1e-4f));
    CHECK_EQ_INT(crowns_room(&crowns, &map, 6, 4, 2, ITEM_APPLES),
                 (int)(CROWN_ROOM_SPARSE / apple + 1e-4f));
    CHECK_EQ_INT(crowns_room(&crowns, &map, 7, 4, 2, ITEM_APPLES), 0); /* Open air. */
    CHECK_EQ_INT(crowns_room(&crowns, &map, 4, 4, 0, ITEM_APPLES), 0); /* The ground. */
    CHECK_EQ_INT(crowns_room(&crowns, &map, -1, 4, 2, ITEM_APPLES), 0);
    CHECK(!crowns_add(&crowns, &map, 7, 4, 2, hanging(ITEM_APPLES, 50, 0)));
    /* Sparse foliage fills up: 4 branches (bulk 10) in 40. */
    for (int i = 0; i < 4; i++) {
        CHECK(crowns_add(&crowns, &map, 6, 4, 2, hanging(ITEM_BRANCHES, 50, 0)));
    }
    CHECK(!crowns_add(&crowns, &map, 6, 4, 2, hanging(ITEM_BRANCHES, 50, 0)));
    CHECK_EQ_INT(crowns_room(&crowns, &map, 6, 4, 2, ITEM_APPLES), 0);
    CHECK(crowns_check(&crowns));
    free_world(&map, &items, &crowns);
}

/* Items hang one by one, are listed newest first, counted and taken. */
static void test_hanging_and_taking(void)
{
    Map map;
    Items items;
    Crowns crowns;
    REQUIRE(init_world(&map, &items, &crowns));
    uint64_t empty = crowns_hash(&crowns, 7);
    CHECK_EQ_INT(empty, 7); /* Nothing hanging: the hash as it was. */
    CrownItem branch = hanging(ITEM_BRANCHES, 40, 3);
    branch.woods = 0x0001;
    CrownItem twig = hanging(ITEM_TWIGS, 40, 3);
    twig.woods = 0x0001; /* Twigs are of no species. */
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_APPLES, 70, 2)));
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, branch));
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_APPLES, 30, 4)));
    REQUIRE(crowns_add(&crowns, &map, 5, 4, 2, twig));
    CHECK_EQ_INT(crowns.count, 4);
    CHECK_EQ_INT(crowns.totals[ITEM_APPLES], 2);
    CHECK(crowns_hash(&crowns, 7) != empty);
    CHECK_EQ_INT(crowns_count(&crowns, 4, 4, 2, item_types_of(ITEM_APPLES)), 2);
    CHECK_EQ_INT(crowns_count(&crowns, 4, 4, 2, item_types_all()), 3);
    CrownItem list[8];
    REQUIRE(crowns_list(&crowns, 4, 4, 2, list, 8) == 3);
    CHECK_EQ_INT(list[0].quality, 30); /* Newest first. */
    CHECK_EQ_INT(list[1].type, ITEM_BRANCHES);
    CHECK_EQ_INT(list[1].woods, 0x0001);
    CHECK_EQ_INT(crowns_list(&crowns, 4, 4, 2, list, 1), 3); /* Only one copied. */
    REQUIRE(crowns_list(&crowns, 5, 4, 2, list, 8) == 1);
    CHECK_EQ_INT(list[0].woods, 0);

    ItemStack stack = crown_item_stack(branch);
    CHECK_EQ_INT(stack.type, ITEM_BRANCHES);
    CHECK_EQ_INT(stack.count, 1);
    CHECK_EQ_INT(stack.quality, 40);
    CHECK_EQ_INT(stack.woods, 0x0001);

    uint32_t revision = crowns.revision;
    CrownItem taken[4];
    CHECK_EQ_INT(crowns_take(&crowns, 4, 4, 2, item_types_of(ITEM_APPLES), taken, 4), 2);
    CHECK(crowns.revision != revision);
    CHECK_EQ_INT(taken[0].quality, 30);
    CHECK_EQ_INT(taken[1].quality, 70);
    CHECK_EQ_INT(crowns_count(&crowns, 4, 4, 2, item_types_all()), 1);
    CHECK_EQ_INT(crowns.totals[ITEM_APPLES], 0);
    CHECK_EQ_INT(crowns_take(&crowns, 4, 4, 2, item_types_of(ITEM_APPLES), taken, 4), 0);
    /* Slots freed are used again. */
    uint32_t used = crowns.pool_used;
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_PEARS, 50, 5)));
    CHECK_EQ_INT(crowns.pool_used, used);
    CHECK(crowns_check(&crowns));
    free_world(&map, &items, &crowns);
}

/* What falls lands on the ground below as an item, as it was. */
static void test_falling(void)
{
    Map map;
    Items items;
    Crowns crowns;
    REQUIRE(init_world(&map, &items, &crowns));
    CHECK_EQ_INT(tree_landing_level(&map, 4, 4, 2), 0);
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_APPLES, 80, 2)));
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_PEARS, 20, 2)));
    CHECK_EQ_INT(crowns_drop_tile(&crowns, &map, &items, 4, 4, 2), 2);
    CHECK_EQ_INT(crowns.count, 0);
    ItemStack apples = items_get_of(&items, 4, 4, 0, item_types_of(ITEM_APPLES));
    CHECK_EQ_INT(apples.count, 1);
    CHECK_EQ_INT(apples.quality, 80);
    CHECK_EQ_INT(items_get_of(&items, 4, 4, 0, item_types_of(ITEM_PEARS)).quality, 20);
    /* Over water: lost. */
    map.water[map_index(&map, 5, 4, 0)] = WATER_DEEP_MM;
    map.water[map_index(&map, 5, 3, 0)] = WATER_DEEP_MM;
    map.water[map_index(&map, 5, 5, 0)] = WATER_DEEP_MM;
    map.water[map_index(&map, 6, 4, 0)] = WATER_DEEP_MM;
    CHECK(!crown_drop(&map, &items, 5, 4, 2, hanging(ITEM_APPLES, 50, 0)));
    CHECK(crowns_check(&crowns));
    free_world(&map, &items, &crowns);
}

/* Fruit falls sooner the longer it hangs and in a storm, all of it out of
 * its season; dead wood about evenly, most of it in a storm. */
static void test_fall_chances(void)
{
    int autumn = SEASON_DAY(SEASON_AUTUMN, 5);
    CrownHour hour = {.day = 20, .day_of_year = autumn};
    CHECK(crown_fruit_in_season(ITEM_APPLES, autumn));
    CHECK(!crown_fruit_in_season(ITEM_APPLES, SEASON_DAY(SEASON_SPRING, 5)));
    CHECK(crown_fruit_in_season(ITEM_BRANCHES, SEASON_DAY(SEASON_SPRING, 5)));
    float fresh = crown_fall_chance(hanging(ITEM_APPLES, 50, 20), &hour);
    float older = crown_fall_chance(hanging(ITEM_APPLES, 50, 15), &hour);
    CHECK_NEAR(fresh, CROWN_FRUIT_FALL_CHANCE, 1e-6);
    CHECK_NEAR(older, CROWN_FRUIT_FALL_CHANCE * 2.0f, 1e-6);
    float dead = crown_fall_chance(hanging(ITEM_TWIGS, 50, 20), &hour);
    CHECK_NEAR(dead, CROWN_DEADWOOD_FALL_CHANCE, 1e-6);
    hour.storm = true;
    CHECK_NEAR(crown_fall_chance(hanging(ITEM_APPLES, 50, 20), &hour),
               CROWN_FRUIT_FALL_CHANCE * CROWN_STORM_FRUIT, 1e-6);
    CHECK_NEAR(crown_fall_chance(hanging(ITEM_BRANCHES, 50, 20), &hour),
               CROWN_DEADWOOD_FALL_CHANCE * CROWN_STORM_DEADWOOD, 1e-6);
    hour.day_of_year = SEASON_DAY(SEASON_WINTER, 1);
    CHECK(crown_fall_chance(hanging(ITEM_APPLES, 50, 20), &hour) >= 1.0f);
}

/* An hour over the pool: what rolls under its chance falls, and so does what
 * hangs where the foliage went; the rolls are the same for the same seed. */
static void test_an_hour(void)
{
    Map map;
    Items items;
    Crowns crowns;
    REQUIRE(init_world(&map, &items, &crowns));
    for (int i = 0; i < 40; i++) {
        REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_TWIGS, 50, 0)));
    }
    REQUIRE(crowns_add(&crowns, &map, 5, 4, 2, hanging(ITEM_APPLES, 50, 0)));
    map.terrain[map_index(&map, 5, 4, 2)] = TERRAIN_OPEN; /* Its foliage is gone. */
    map.water[map_index(&map, 5, 4, 2)] = 0;
    CrownHour hour = {.seed = 42,
                      .hour = 100,
                      .day = 1,
                      .day_of_year = SEASON_DAY(SEASON_AUTUMN, 5),
                      .storm = true}; /* Twigs: a 20 % chance each. */
    int fell = crowns_hour(&crowns, &map, &items, 0, crowns.pool_used, &hour);
    CHECK_EQ_INT(crowns_count(&crowns, 5, 4, 2, item_types_all()), 0);
    CHECK_EQ_INT(items_get_of(&items, 5, 4, 0, item_types_of(ITEM_APPLES)).count, 1);
    CHECK(fell > 1 && fell < 30);
    CHECK_EQ_INT(crowns.count, 41 - fell);
    CHECK_EQ_INT((int)(items.totals[ITEM_TWIGS]), fell - 1);
    CHECK(crowns_check(&crowns));
    /* A slice of the pool: only those slots. */
    long before = crowns.count;
    hour.hour++;
    crowns_hour(&crowns, &map, &items, 0, 0, &hour);
    CHECK_EQ_INT(crowns.count, before);
    free_world(&map, &items, &crowns);
}

/* A picture of the crowns (the known world's) copied tile by tile: the same
 * lists in the same order, other tiles left as they were; copying again
 * what agrees changes nothing. */
static void test_copying_a_tile(void)
{
    Map map;
    Items items;
    Crowns crowns, picture;
    REQUIRE(init_world(&map, &items, &crowns));
    REQUIRE(crowns_init(&picture, SIZE, SIZE, DEPTH));
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_APPLES, 40, 1)));
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_BRANCHES, 60, 2)));
    REQUIRE(crowns_add(&crowns, &map, 4, 4, 2, hanging(ITEM_APPLES, 70, 3)));
    REQUIRE(crowns_add(&crowns, &map, 5, 4, 2, hanging(ITEM_PEARS, 50, 3)));
    size_t dense = map_index(&map, 4, 4, 2), medium = map_index(&map, 5, 4, 2);
    REQUIRE(crowns_copy_tile(&picture, &crowns, dense));
    CHECK_EQ_INT(picture.count, 3);
    CHECK_EQ_INT(crowns_count(&picture, 5, 4, 2, item_types_of(ITEM_PEARS)), 0); /* Not copied. */
    CrownItem real[4], copy[4];
    REQUIRE(crowns_list(&crowns, 4, 4, 2, real, 4) == 3);
    REQUIRE(crowns_list(&picture, 4, 4, 2, copy, 4) == 3);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ_INT(copy[i].type, real[i].type);
        CHECK_EQ_INT(copy[i].quality, real[i].quality);
        CHECK_EQ_INT(copy[i].day, real[i].day);
    }
    uint32_t revision = picture.revision;
    REQUIRE(crowns_copy_tile(&picture, &crowns, dense));
    CHECK_EQ_UINT(picture.revision, revision); /* Nothing changed. */
    /* Picked in the real crowns: the picture follows when copied again. */
    CrownItem taken[4];
    CHECK_EQ_INT(crowns_take(&crowns, 4, 4, 2, item_types_of(ITEM_APPLES), taken, 4), 2);
    CHECK_EQ_INT(crowns_count(&picture, 4, 4, 2, item_types_of(ITEM_APPLES)), 2);
    REQUIRE(crowns_copy_tile(&picture, &crowns, dense));
    REQUIRE(crowns_copy_tile(&picture, &crowns, medium));
    CHECK_EQ_INT(crowns_count(&picture, 4, 4, 2, item_types_of(ITEM_APPLES)), 0);
    CHECK_EQ_INT(crowns_count(&picture, 4, 4, 2, item_types_of(ITEM_BRANCHES)), 1);
    CHECK_EQ_INT(picture.totals[ITEM_PEARS], 1);
    CHECK(crowns_hash(&picture, 0) == crowns_hash(&crowns, 0));
    CHECK(crowns_check(&picture));
    crowns_free(&picture);
    free_world(&map, &items, &crowns);
}

int main(void)
{
    RUN_TEST(test_room);
    RUN_TEST(test_hanging_and_taking);
    RUN_TEST(test_falling);
    RUN_TEST(test_fall_chances);
    RUN_TEST(test_an_hour);
    RUN_TEST(test_copying_a_tile);
    return test_summary();
}
